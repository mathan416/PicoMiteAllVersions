#include "stub.h"
#include "MMtftp.c"
#include "tftp.c"

static unsigned pbuf_live,udp_live,sends,allocations;
static int allocation_fail=-1,udp_failure,bind_failure;
static u32_t now;
static int timer_pending;
static void (*timer_fn)(void*);static void *timer_arg;
static uint8_t sent[1024];static u16_t sent_len;
static struct udp_pcb pcb;
static unsigned fat_closes,fat_mounts,fat_reads,fat_writes,fat_syncs;
static int fat_open_error,fat_read_error,fat_write_error,fat_short_write,fat_sync_error,fat_mount_error;
static uint8_t fat_data[4096];static size_t fat_size,fat_offset;
static char fat_path[STRINGSIZE];
FATFS FatFs;volatile BYTE SDCardStat;
int FatFSFileSystemSave;
char filepath[2][FF_MAX_LFN]={"A:/","B:/"};
struct test_options Option={1,0,1,0,1,0,1,0};
lfs_t lfs;
static uint8_t media[12*4096],read_cache[256],write_cache[256],look_cache[256];
static int little_read_error,little_prog_error,little_sync_error;
static unsigned sync_before_ack;
void *GetMemory(int size){(void)size;assert(!"TFTP used BASIC heap");return NULL;}
void FreeMemory(unsigned char *p){(void)p;assert(!"TFTP freed BASIC heap");}
DWORD get_fattime(void){return 0x12345678u;}
static int media_read(const struct lfs_config*c,lfs_block_t b,lfs_off_t off,void*buf,lfs_size_t n){(void)c;if(little_read_error)return LFS_ERR_IO;assert((size_t)b*4096+off+n<=sizeof media);memcpy(buf,media+b*4096+off,n);return 0;}
static int media_prog(const struct lfs_config*c,lfs_block_t b,lfs_off_t off,const void*buf,lfs_size_t n){(void)c;if(little_prog_error)return LFS_ERR_IO;assert((size_t)b*4096+off+n<=sizeof media);for(unsigned i=0;i<n;i++)media[b*4096+off+i]&=((const uint8_t*)buf)[i];return 0;}
static int media_erase(const struct lfs_config*c,lfs_block_t b){(void)c;if(little_prog_error)return LFS_ERR_IO;assert((size_t)(b+1)*4096<=sizeof media);memset(media+b*4096,255,4096);return 0;}
static int media_sync(const struct lfs_config*c){(void)c;sync_before_ack++;return little_sync_error?LFS_ERR_IO:0;}
struct lfs_config pico_lfs_cfg={.read=media_read,.prog=media_prog,.erase=media_erase,.sync=media_sync,.read_size=1,.prog_size=256,.block_size=4096,.block_count=12,.block_cycles=500,.cache_size=256,.lookahead_size=256,.read_buffer=read_cache,.prog_buffer=write_cache,.lookahead_buffer=look_cache};
FRESULT f_mount(FATFS*fs,const TCHAR*path,BYTE opt){(void)fs;(void)path;(void)opt;fat_mounts++;if(fat_mount_error)return FR_DISK_ERR;SDCardStat=0;return FR_OK;}
FRESULT f_open(FIL*f,const TCHAR*path,BYTE mode){(void)f;snprintf(fat_path,sizeof fat_path,"%s",path);if(fat_open_error)return FR_NO_FILE;if(strstr(path,"missing"))return FR_NO_FILE;if(strstr(path,"absent"))return FR_NO_PATH;fat_offset=0;if(mode&FA_CREATE_ALWAYS)fat_size=0;return FR_OK;}
FRESULT f_close(FIL*f){(void)f;fat_closes++;return FR_OK;}
FRESULT f_read(FIL*f,void*out,UINT n,UINT*actual){(void)f;fat_reads++;*actual=0xdead;if(fat_read_error)return FR_DISK_ERR;size_t available=fat_size-fat_offset;if(n>available)n=available;memcpy(out,fat_data+fat_offset,n);fat_offset+=n;*actual=n;return FR_OK;}
FRESULT f_write(FIL*f,const void*in,UINT n,UINT*actual){(void)f;fat_writes++;*actual=0;if(fat_write_error)return FR_DISK_ERR;if(fat_short_write&&n)n--;if(n>sizeof fat_data-fat_offset)n=sizeof fat_data-fat_offset;memcpy(fat_data+fat_offset,in,n);fat_offset+=n;if(fat_offset>fat_size)fat_size=fat_offset;*actual=n;return FR_OK;}
FRESULT f_sync(FIL*f){(void)f;fat_syncs++;sync_before_ack++;return fat_sync_error?FR_DISK_ERR:FR_OK;}

static struct pbuf *makebuf(u16_t len){struct pbuf*p=calloc(1,sizeof *p);assert(p);p->allocation=calloc(1,len?len:1);assert(p->allocation);p->payload=p->allocation;p->len=p->tot_len=len;pbuf_live++;return p;}
struct pbuf *pbuf_alloc(int layer,u16_t n,int type){(void)layer;(void)type;allocations++;if(allocation_fail==0)return NULL;if(allocation_fail>0)allocation_fail--;return makebuf(n);}
u8_t pbuf_free(struct pbuf*p){u8_t n=0;while(p){struct pbuf*next=p->next;free(p->allocation);free(p);assert(pbuf_live);pbuf_live--;n++;p=next;}return n;}
void pbuf_realloc(struct pbuf*p,u16_t n){assert(!p->next&&n<=p->tot_len);p->len=p->tot_len=n;}
u16_t pbuf_copy_partial(const struct pbuf*p,void*out,u16_t n,u16_t off){u16_t copied=0;while(p&&off>=p->len){off-=p->len;p=p->next;}while(p&&copied<n){u16_t take=p->len-off;if(take>n-copied)take=n-copied;memcpy((uint8_t*)out+copied,(uint8_t*)p->payload+off,take);copied+=take;off=0;p=p->next;}return copied;}
err_t pbuf_copy(struct pbuf*out,const struct pbuf*in){assert(!out->next&&out->len>=in->tot_len);return pbuf_copy_partial(in,out->payload,in->tot_len,0)==in->tot_len?ERR_OK:ERR_VAL;}
u16_t pbuf_memfind(const struct pbuf*p,const void*needle,u16_t n,u16_t off){assert(n==1);for(u16_t i=off;i<p->tot_len;i++){uint8_t byte;if(pbuf_copy_partial(p,&byte,1,i)!=1)return 0xffff;if(byte==*(const uint8_t*)needle)return i;}return 0xffff;}
struct udp_pcb *udp_new_ip_type(int type){(void)type;if(udp_failure)return NULL;udp_live++;return &pcb;}
err_t udp_bind(struct udp_pcb*p,const ip_addr_t*addr,u16_t port){(void)p;(void)addr;assert(port==69);return bind_failure?ERR_USE:ERR_OK;}
void udp_remove(struct udp_pcb*p){assert(p==&pcb&&udp_live);udp_live--;}
void udp_recv(struct udp_pcb*p,void(*f)(void*,struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t),void*arg){(void)p;(void)f;(void)arg;}
err_t udp_sendto(struct udp_pcb*p,struct pbuf*buf,const ip_addr_t*addr,u16_t port){(void)p;(void)addr;(void)port;assert(buf->tot_len<=sizeof sent);sent_len=pbuf_copy_partial(buf,sent,buf->tot_len,0);sends++;return ERR_OK;}
void sys_timeout(u32_t ms,void(*f)(void*),void*arg){assert(ms==1000&&!timer_pending);timer_pending=1;timer_fn=f;timer_arg=arg;}
void sys_untimeout(void(*f)(void*),void*arg){(void)f;(void)arg;timer_pending=0;}
u32_t sys_now(void){return now;}
static void tick(unsigned seconds){for(unsigned i=0;i<seconds;i++){now+=1000;if(timer_pending){void(*f)(void*)=timer_fn;void*arg=timer_arg;timer_pending=0;f(arg);}}}
static unsigned opcode(void){assert(sent_len>=4);return ((unsigned)sent[0]<<8)|sent[1];}
static unsigned number(void){assert(sent_len>=4);return ((unsigned)sent[2]<<8)|sent[3];}
static void receive_raw(const void*data,u16_t n,int split,unsigned port){struct pbuf*p;if(split>=0&&split<n){p=makebuf(split);p->next=makebuf(n-split);p->tot_len=n;memcpy(p->payload,data,split);memcpy(p->next->payload,(const uint8_t*)data+split,n-split);}else{p=makebuf(n);memcpy(p->payload,data,n);}ip_addr_t peer={42};tftp_recv(NULL,&pcb,p,&peer,port);}
static void request(unsigned op,const char*name,const char*mode,int split,unsigned port){uint8_t data[1024];size_t n=strlen(name),m=strlen(mode);assert(n+m+4<=sizeof data);data[0]=0;data[1]=op;memcpy(data+2,name,n+1);memcpy(data+n+3,mode,m+1);receive_raw(data,n+m+4,split,port);}
static void data_packet(unsigned op,unsigned block,const void*payload,u16_t bytes,int split,unsigned port){uint8_t data[1024];assert(bytes+4<=sizeof data);data[0]=0;data[1]=op;data[2]=block>>8;data[3]=block;memcpy(data+4,payload,bytes);receive_raw(data,bytes+4,split,port);}
static void idle(void){assert(!tftp_state.handle&&!tftp_file.active&&!timer_pending&&!pbuf_live);}
static void cancel(void){if(tftp_state.handle)data_packet(TFTP_ERROR,0,"cancel",7,-1,123);idle();}
static void put_good(const char*name){request(TFTP_WRQ,name,"octet",-1,123);assert(opcode()==TFTP_ACK&&number()==0);data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ACK&&number()==1);idle();}
static void freshfs(void){assert(!tftp_file.active);(void)lfs_unmount(&lfs);memset(media,255,sizeof media);little_read_error=little_prog_error=little_sync_error=0;assert(!lfs_format(&lfs,&pico_lfs_cfg));assert(!lfs_mount(&lfs,&pico_lfs_cfg));}

int main(void){
 memset(media,255,sizeof media);assert(!lfs_format(&lfs,&pico_lfs_cfg));assert(!lfs_mount(&lfs,&pico_lfs_cfg));
 udp_failure=1;assert(!cmd_tftp_server_init()&&!udp_live);udp_failure=0;bind_failure=1;assert(!cmd_tftp_server_init()&&!udp_live);bind_failure=0;assert(cmd_tftp_server_init()&&udp_live==1);assert(!cmd_tftp_server_init()&&udp_live==1);idle();
 for(unsigned i=0;i<200;i++){request(TFTP_RRQ,"A:/missing.bin","octet",i%8,123);assert(opcode()==TFTP_ERROR);idle();}
 put_good("A:/test.bin");request(TFTP_RRQ,"A:/test.bin","OCTET",1,123);assert(opcode()==TFTP_DATA&&sent_len==7&&!memcmp(sent+4,"abc",3));data_packet(TFTP_ACK,1,"",0,2,123);idle();
 uint32_t stamp=0;assert(lfs_getattr(&lfs,"/test.bin",'A',&stamp,sizeof stamp)==4&&stamp==get_fattime());
 puts("PASS: repeated missing files return protocol errors, then valid transfers and attributes work; no BASIC heap/file wrappers");
 // Filename and mode boundaries, missing terminators/options, invalid drives/paths.
 uint8_t bad[]={0,1,'x','x'};for(unsigned i=0;i<200;i++){receive_raw(bad,sizeof bad,-1,123);assert(opcode()==TFTP_ERROR);idle();}
 char longname[65];memset(longname,'x',64);longname[64]=0;request(TFTP_WRQ,longname,"octet",-1,123);assert(opcode()==TFTP_ERROR);idle();
 char maxname[64];memset(maxname,'m',63);maxname[63]=0;request(TFTP_WRQ,maxname,"octet",-1,123);assert(opcode()==TFTP_ACK);cancel();
 const char*badnames[]={"","Z:/x","A:","A:/../x","A:/bad\\x","A:/foo*","A:/absent/x"};
 for(unsigned i=0;i<sizeof badnames/sizeof badnames[0];i++){request(TFTP_WRQ,badnames[i],"octet",-1,123);assert(opcode()==TFTP_ERROR);idle();}
 request(TFTP_WRQ,"A:/x","netascii",-1,123);assert(opcode()==TFTP_ERROR);idle();
 uint8_t trailing[]={0,2,'x',0,'o','c','t','e','t',0,'x',0};receive_raw(trailing,sizeof trailing,-1,123);assert(opcode()==TFTP_ERROR);idle();
 char path[STRINGSIZE];bool fat;
 FatFSFileSystemSave=1;strcpy(filepath[1],"B:/pico2-apps/");
 assert(tftp_path("nested/../test.bin",path,&fat)&&fat&&!strcmp(path,"/pico2-apps/test.bin"));
 assert(tftp_path("../test.bin",path,&fat)&&!strcmp(path,"/test.bin"));
 assert(!tftp_path("../../test.bin",path,&fat));
 assert(tftp_path("A:/test.bin",path,&fat)&&!fat&&!strcmp(path,"/test.bin"));
 strcpy(filepath[1],"B:/");FatFSFileSystemSave=0;
 unsigned cache_size=pico_lfs_cfg.cache_size;pico_lfs_cfg.cache_size=512;
 request(TFTP_WRQ,"A:/bad-cache.bin","octet",-1,123);assert(opcode()==TFTP_ERROR);idle();pico_lfs_cfg.cache_size=cache_size;
 puts("PASS: malformed/overlong/unsupported requests do not leak timers; paths and cache bounds are enforced");
 // Chained headers and data; duplicate DATA does not append twice.
 for(unsigned split=0;split<7;split++){request(TFTP_WRQ,"A:/split.bin","octet",split,123);assert(opcode()==TFTP_ACK);unsigned synced=sync_before_ack;data_packet(TFTP_DATA,1,"abc",3,split,123);assert(opcode()==TFTP_ACK&&sync_before_ack>synced);idle();data_packet(TFTP_DATA,1,"abc",3,split,123);assert(opcode()==TFTP_ACK);idle();}
 request(TFTP_RRQ,"A:/split.bin","octet",-1,123);assert(sent_len==7&&!memcmp(sent+4,"abc",3));cancel();
 // A lost initial reply repeats only for the same request; another peer cannot reset timeout.
 request(TFTP_WRQ,"A:/retry.bin","octet",-1,123);request(TFTP_WRQ,"A:/retry.bin","octet",-1,123);assert(opcode()==TFTP_ACK&&number()==0);
 request(TFTP_WRQ,"A:/other.bin","octet",-1,123);assert(opcode()==TFTP_ERROR&&tftp_file.active);
 request(TFTP_WRQ,"A:/other.bin","octet",-1,124);assert(opcode()==TFTP_ERROR&&tftp_file.active);tick(12);idle();
 // Timeout and cancellation release a file exactly once and allow immediate reuse.
 request(TFTP_RRQ,"A:/test.bin","octet",-1,123);tick(20);idle();put_good("A:/after-timeout.bin");
 request(TFTP_WRQ,"A:/cancel.bin","octet",-1,123);data_packet(TFTP_ERROR,0,"\xff",1,1,123);idle();put_good("A:/after-cancel.bin");
 request(TFTP_WRQ,"A:/bad-chain.bin","octet",-1,123);
 struct pbuf*bad_chain=makebuf(7);uint8_t raw[]={0,3,0,1,'a','b','c'};memcpy(bad_chain->payload,raw,7);bad_chain->tot_len=10;
 ip_addr_t peer={42};tftp_recv(NULL,&pcb,bad_chain,&peer,123);assert(opcode()==TFTP_ERROR);idle();
 puts("PASS: split/chained packets, duplicate replies, competing peers, timeout and cancellation recover cleanly");
 // Inject actual LittleFS read/program/sync errors and a genuinely full volume.
 request(TFTP_WRQ,"A:/sync.bin","octet",-1,123);little_sync_error=1;data_packet(TFTP_DATA,1,"payload",7,-1,123);assert(opcode()==TFTP_ERROR);little_sync_error=0;idle();put_good("A:/after-sync.bin");
 request(TFTP_WRQ,"A:/prog.bin","octet",-1,123);little_prog_error=1;uint8_t chunk[512];memset(chunk,0x33,sizeof chunk);data_packet(TFTP_DATA,1,chunk,512,-1,123);assert(opcode()==TFTP_ERROR);little_prog_error=0;idle();
 little_read_error=1;request(TFTP_RRQ,"A:/missing.bin","octet",-1,123);assert(opcode()==TFTP_ERROR);little_read_error=0;idle();
 freshfs();request(TFTP_WRQ,"A:/full.bin","octet",-1,123);unsigned block=1;for(;block<150;block++){data_packet(TFTP_DATA,block,chunk,512,-1,123);if(opcode()==TFTP_ERROR)break;assert(opcode()==TFTP_ACK);}assert(block<150);idle();freshfs();put_good("A:/after-full.bin");
 puts("PASS: real LittleFS I/O/sync/full-volume errors are not ACKed as success and do not poison the next request");
 // FAT failures, short writes, missing/unconfigured card and explicit drive routing.
 FatFSFileSystemSave=0;SDCardStat=STA_NOINIT;fat_mount_error=1;request(TFTP_WRQ,"B:/test.bin","octet",-1,123);assert(opcode()==TFTP_ERROR);idle();fat_mount_error=0;put_good("B:/test.bin");assert(!strcmp(fat_path,"/test.bin")&&FatFSFileSystemSave==0);
 fat_read_error=1;request(TFTP_RRQ,"B:/test.bin","octet",-1,123);assert(opcode()==TFTP_ERROR);fat_read_error=0;idle();
 request(TFTP_WRQ,"B:/short.bin","octet",-1,123);unsigned closes=fat_closes;fat_short_write=1;data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ERROR&&fat_closes==closes+1);fat_short_write=0;idle();
 request(TFTP_WRQ,"B:/sync.bin","octet",-1,123);fat_sync_error=1;data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ERROR);fat_sync_error=0;idle();
 request(TFTP_WRQ,"B:/fail.bin","octet",-1,123);fat_write_error=1;data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ERROR);fat_write_error=0;idle();
 request(TFTP_WRQ,"B:/cancel.bin","octet",-1,123);closes=fat_closes;cancel();assert(fat_closes==closes+1);tftp_close(&tftp_file);assert(fat_closes==closes+1);
 Option.SD_CS=0;request(TFTP_WRQ,"B:/test.bin","octet",-1,123);assert(opcode()==TFTP_ERROR);idle();put_good("A:/without-sd.bin");Option.SD_CS=1;
 FatFSFileSystemSave=1;put_good("A:/explicit.bin");assert(FatFSFileSystemSave==1);FatFSFileSystemSave=0;
 puts("PASS: FAT read/write/sync/short-write and card failures return clean errors; independent handles and drive routing preserved");
 // No per-file allocation even when network allocations fail; all retained state times out.
 allocation_fail=0;request(TFTP_RRQ,"A:/after-full.bin","octet",-1,123);allocation_fail=-1;idle();put_good("A:/after-oom.bin");
 request(TFTP_WRQ,"A:/ack-oom.bin","octet",-1,123);allocation_fail=0;data_packet(TFTP_DATA,1,"abc",3,-1,123);allocation_fail=-1;idle();data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ACK);idle();
 // Duplicate ACK of a preceding full block is ignored, not an error or resend.
 request(TFTP_WRQ,"A:/two-block.bin","octet",-1,123);data_packet(TFTP_DATA,1,chunk,512,-1,123);data_packet(TFTP_DATA,2,"x",1,-1,123);idle();
 request(TFTP_RRQ,"A:/two-block.bin","octet",-1,123);little_read_error=1;data_packet(TFTP_ACK,1,"",0,-1,123);assert(opcode()==TFTP_ERROR);little_read_error=0;idle();
 request(TFTP_RRQ,"A:/two-block.bin","octet",-1,123);data_packet(TFTP_ACK,1,"",0,-1,123);unsigned before=sends;data_packet(TFTP_ACK,1,"",0,-1,123);assert(sends==before);data_packet(TFTP_ACK,2,"",0,-1,123);idle();
 // Deterministic malformed-packet sweep; check resources after every burst.
 uint32_t seed=0x12345678;for(unsigned i=0;i<5000;i++){uint8_t data[600];seed=seed*1664525+1013904223;unsigned n=seed%sizeof data;for(unsigned j=0;j<n;j++){seed=seed*1664525+1013904223;data[j]=seed>>24;}if(n>=2&&i%3==0){data[0]=0;data[1]=1+i%6;}receive_raw(data,n,n>1?(int)(i%n):-1,123);if(tftp_state.handle)tick(20);idle();}
 // Completed-upload dally must expire, handle wrap and leave other peers alone.
 put_good("A:/dally.bin");u32_t finished=tftp_completed_write.time;now=finished+19999;
 data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ACK);idle();
 now=finished+20000;data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ERROR);idle();
 put_good("A:/dally.bin");request(TFTP_WRQ,"A:/peer.bin","octet",-1,124);
 data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ACK&&tftp_state.port==124&&tftp_file.active);
 data_packet(TFTP_ERROR,0,"cancel",7,-1,124);idle();
 put_good("A:/dally.bin");tftp_completed_write.time=0xfffffff0u;now=32;
 data_packet(TFTP_DATA,1,"abc",3,-1,123);assert(opcode()==TFTP_ACK);idle();
 request(TFTP_WRQ,"A:/wrap.bin","octet",-1,123);tftp_state.timer=UINT32_MAX-3;tftp_state.last_pkt=tftp_state.timer;tick(12);idle();
 puts("PASS: allocation failures, dally expiry/timer wrap, peer isolation, duplicate ACKs and 5000 malformed packets leave no resources behind");
 tftp_cleanup();idle();assert(!udp_live);assert(!lfs_unmount(&lfs));
}
