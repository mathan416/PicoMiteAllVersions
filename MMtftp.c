#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "ff.h"
#include "diskio.h"
#include "lwip/apps/tftp_common.h"
#include "lwip/def.h"
#include "lwip/apps/tftp_server.h"

/* Network callbacks must never invoke BASIC error(), allocate from its heap,
 * change its current drive, or close a program's file number. */
extern FATFS FatFs;
extern volatile BYTE SDCardStat;
extern char filepath[2][FF_MAX_LFN];
static struct {
    union { FIL fat; lfs_file_t little; } file;
    struct lfs_file_config config;
    struct lfs_attr attr;
    uint32_t timestamp;
    uint8_t cache[256];
    bool active, fat, writing;
} tftp_file;
static struct tftp_context ctx;
#if FF_USE_LFN == 3
#error TFTP callbacks require non-allocating FatFS name buffers
#endif

static int tftp_path(const char *name, char path[STRINGSIZE], bool *fat) {
    int drive = FatFSFileSystemSave;
    char joined[STRINGSIZE];
    if (!name || !*name || drive < 0 || drive > 1) return 0;
    if (name[1] == ':') {
        if (name[0] == 'A' || name[0] == 'a') drive = 0;
        else if (name[0] == 'B' || name[0] == 'b') drive = 1;
        else return 0;
        name += 2;
        if (*name != '/') return 0;
    }
    const char *cwd = filepath[drive];
    if (cwd[0] && cwd[1] == ':') cwd += 2;
    int length = *name == '/' ? snprintf(joined, sizeof joined, "%s", name)
                            : snprintf(joined, sizeof joined, "%s/%s", cwd, name);
    if (length < 0 || (size_t)length >= sizeof joined) return 0;
    size_t used = 1;
    path[0] = '/'; path[1] = 0;
    const char *part = joined;
    while (*part) {
        while (*part == '/') ++part;
        const char *end = part;
        while (*end && *end != '/') {
            unsigned char c = (unsigned char)*end++;
            if (c < 32 || c == 127 || strchr("\\:*?\"<>|", c)) return 0;
        }
        size_t count = (size_t)(end - part);
        if (!count) break;
        if (count == 1 && part[0] == '.') { part = end; continue; }
        if (count == 2 && part[0] == '.' && part[1] == '.') {
            if (used == 1) return 0;
            while (used > 1 && path[used - 1] != '/') --used;
            if (used > 1) --used;
            path[used] = 0;
        } else {
            if (drive && count > FF_MAX_LFN) return 0;
            if (used + (used > 1) + count >= STRINGSIZE) return 0;
            if (used > 1) path[used++] = '/';
            memcpy(path + used, part, count); used += count; path[used] = 0;
        }
        part = end;
    }
    *fat = drive != 0;
    return used > 1;
}

static int tftp_sd_ready(void) {
    if ((IsInvalidPin(Option.SD_CS) && !Option.CombinedCS) ||
        (IsInvalidPin(Option.SYSTEM_MOSI) && IsInvalidPin(Option.SD_MOSI_PIN)) ||
        (IsInvalidPin(Option.SYSTEM_MISO) && IsInvalidPin(Option.SD_MISO_PIN)) ||
        (IsInvalidPin(Option.SYSTEM_CLK) && IsInvalidPin(Option.SD_CLK_PIN))) return 0;
    return !(SDCardStat & STA_NOINIT) || f_mount(&FatFs, "", 1) == FR_OK;
}

static void *tftp_open(const char *name, const char *mode, u8_t writing) {
    char path[STRINGSIZE]; bool fat;
    if (tftp_file.active || !mode || lwip_stricmp(mode, "octet") ||
        !tftp_path(name, path, &fat)) return NULL;
    memset(&tftp_file, 0, sizeof tftp_file);
    tftp_file.fat = fat; tftp_file.writing = writing != 0;
    if (fat) {
        if (!tftp_sd_ready() || f_open(&tftp_file.file.fat, path,
                writing ? FA_WRITE | FA_CREATE_ALWAYS : FA_READ) != FR_OK) return NULL;
    } else {
        if (pico_lfs_cfg.cache_size > sizeof tftp_file.cache) return NULL;
        tftp_file.config.buffer = tftp_file.cache;
        if (writing) {
            tftp_file.timestamp = get_fattime();
            tftp_file.attr.type = 'A'; tftp_file.attr.buffer = &tftp_file.timestamp;
            tftp_file.attr.size = sizeof tftp_file.timestamp;
            tftp_file.config.attrs = &tftp_file.attr; tftp_file.config.attr_count = 1;
        }
        if (lfs_file_opencfg(&lfs, &tftp_file.file.little, path,
                writing ? LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC : LFS_O_RDONLY,
                &tftp_file.config) < 0) return NULL;
    }
    tftp_file.active = true;
    return &tftp_file;
}

static void tftp_close(void *handle) {
    if (handle != &tftp_file || !tftp_file.active) return;
    tftp_file.active = false;
    if (tftp_file.fat) (void)f_close(&tftp_file.file.fat);
    else (void)lfs_file_close(&lfs, &tftp_file.file.little);
}

static int tftp_read(void *handle, void *buf, int bytes) {
    if (handle != &tftp_file || !tftp_file.active || tftp_file.writing || bytes < 0 || (bytes && !buf)) return -1;
    if (tftp_file.fat) {
        UINT count = 0;
        if (f_read(&tftp_file.file.fat, buf, (UINT)bytes, &count) != FR_OK || count > (UINT)bytes) return -1;
        return (int)count;
    }
    return lfs_file_read(&lfs, &tftp_file.file.little, buf, (lfs_size_t)bytes);
}

static int tftp_write(void *handle, struct pbuf *p) {
    if (handle != &tftp_file || !tftp_file.active || !tftp_file.writing || !p || p->tot_len > 512) return -1;
    unsigned count = 0, segments = 0;
    for (const struct pbuf *q = p; q; q = q->next) {
        if (++segments > 513 || q->len > p->tot_len - count || (q->len && !q->payload)) return -1;
        count += q->len;
    }
    if (count != p->tot_len) return -1;
    for (const struct pbuf *q = p; q; q = q->next) {
        if (!q->len) continue;
        if (tftp_file.fat) {
            UINT actual = 0;
            if (f_write(&tftp_file.file.fat, q->payload, q->len, &actual) != FR_OK || actual != q->len) return -1;
        } else if (lfs_file_write(&lfs, &tftp_file.file.little, q->payload, q->len) != q->len) return -1;
    }
    /* The final ACK must follow a successful sync, including exact-multiple
     * uploads whose terminating DATA packet has an empty payload. */
    if (count < 512) {
        if (tftp_file.fat) { if (f_sync(&tftp_file.file.fat) != FR_OK) return -1; }
        else if (lfs_file_sync(&lfs, &tftp_file.file.little) < 0) return -1;
    }
    return (int)count;
}

static void tftp_error(void *handle, int err, const char *msg, int size) {
    /* The protocol owns closing. Peer messages may be split/non-terminated;
     * never print them as a BASIC C string or close the file a second time. */
    (void)handle; (void)err; (void)msg; (void)size;
}

int cmd_tftp_server_init(void) {
    ctx.open = tftp_open; ctx.close = tftp_close; ctx.error = tftp_error;
    ctx.write = tftp_write; ctx.read = tftp_read;
    return tftp_init_server(&ctx) == ERR_OK;
}
