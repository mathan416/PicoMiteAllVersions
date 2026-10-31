/* Preserve SD4/SD5 A: contents when correcting their oversized geometry. */
#include <stdint.h>
#include "lfs.h"
#include "LoaderFlash.h"
int loader_filesystem_recover(lfs_t *fs, struct lfs_config *cfg, int mount_error) {
    if (!mount_error) return 0;
    lfs_size_t actual = cfg->block_count;
    /* SD4/SD5 used a 16 MB compile maximum minus the 8 KB loader. The
     * filesystem start is unchanged in this repair. Read callbacks enforce
     * the physical boundary, and program/erase reject oversized geometry. */
    uint32_t start = loader_flash_size() - actual * cfg->block_size;
    if (mount_error == LFS_ERR_INVAL) {
        cfg->block_count = (16u*1024u*1024u - 8192u - start) / cfg->block_size;
        if (!lfs_mount(fs, cfg)) return 1;
        cfg->block_count = actual;
    }
    /* Format only an entirely erased filesystem region. A corrupt or
     * incompatible filesystem is preserved, never interpreted as blank. */
    uint8_t bytes[256];
    for (uint32_t block=0; block<actual; ++block) {
        for (uint32_t off=0; off<cfg->block_size; off+=sizeof bytes) {
            if (cfg->read(cfg, block, off, bytes, sizeof bytes)) return LFS_ERR_IO;
            for (unsigned i=0; i<sizeof bytes; ++i)
                if (bytes[i] != 0xff) return mount_error;
        }
    }
    int result = lfs_format(fs, cfg);
    return result ? result : lfs_mount(fs, cfg);
}
