#!/usr/bin/env python3
"""Run the real TFTP receive state machine with packet-loss/retry cases."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];s=(root/'tftp.c').read_text()
def function(marker):
 a=s.index(marker);a=s.rfind('\n',0,a)+1;a=s.rfind('\n',0,a-1)+1;start=s.index('{',a);level=1;end=start+1
 while level:
  level+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[a:end]
program=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <arpa/inet.h>
typedef uint8_t u8_t;typedef uint16_t u16_t;typedef uint32_t u32_t;typedef int err_t;typedef struct {unsigned addr;} ip_addr_t;
#define ip_addr_eq(a,b) ((a)->addr==(b)->addr)
#define ip_addr_isany_val(a) (!(a).addr)
#define ip_addr_copy(a,b) ((a)=(b))
#define LWIP_UNUSED_ARG(x) ((void)(x))
#define PP_HTONS(x) htons(x)
#define lwip_ntohs(x) ntohs(x)
#define LWIP_DEBUGF(a,b) ((void)0)
#define ip_addr_debug_print(a,b) ((void)0)
#define TFTP_HEADER_LENGTH 4
#define TFTP_MAX_PAYLOAD_SIZE 512
#define TFTP_MAX_FILENAME_LEN 255
#define TFTP_MAX_MODE_LEN 16
#define TFTP_TIMEOUT_MSECS 10000
#define TFTP_TIMER_MSECS 1000
#define LWIP_TFTP_MODE_SERVER 1
#define TFTP_RRQ 1
#define TFTP_WRQ 2
#define TFTP_DATA 3
#define TFTP_ACK 4
#define TFTP_ERROR 5
#define TFTP_ERROR_ACCESS_VIOLATION 2
#define TFTP_ERROR_UNKNOWN_TRFR_ID 5
#define TFTP_ERROR_FILE_NOT_FOUND 1
#define TFTP_ERROR_ILLEGAL_OPERATION 4
struct udp_pcb{int unused;};
struct pbuf{u16_t tot_len,len;unsigned char *payload,*allocation;};
struct tftp_context{void *(*open)(const char*,const char*,u8_t);int(*write)(void*,struct pbuf*);void(*error)(void*,int,const char*,int);};
struct tftp_state{const struct tftp_context *ctx;void *handle;struct pbuf *last_data;struct udp_pcb *upcb;ip_addr_t addr;u16_t port;int timer,last_pkt;u16_t blknum;u8_t retries,mode_write,tftp_mode;};
static struct tftp_state tftp_state;
static u32_t now;static unsigned acks,last_ack,writes,closes,opens,resends,errors,freed;
static int write_failure;
static u32_t sys_now(void){return now;}
static void send_ack(const ip_addr_t*a,u16_t p,u16_t b){(void)a;(void)p;acks++;last_ack=b;}
static void send_error(const ip_addr_t*a,u16_t p,int code,const char*m){(void)a;(void)p;(void)code;(void)m;errors++;}
static void resend_data(const ip_addr_t*a,u16_t p){(void)a;(void)p;resends++;}
static void send_data(const ip_addr_t*a,u16_t p){(void)a;(void)p;resends++;}
static void tftp_tmr(void*a){(void)a;}
static void sys_timeout(int ms,void(*f)(void*),void*a){(void)ms;(void)f;(void)a;}
static void pbuf_free(struct pbuf*p){free(p->allocation);free(p);freed++;}
static u16_t pbuf_copy_partial(const struct pbuf*p,void*out,u16_t len,u16_t off){if(off>p->tot_len)return 0;if(len>p->tot_len-off)len=p->tot_len-off;memcpy(out,p->payload+off,len);return len;}
static u16_t pbuf_memfind(const struct pbuf*p,const void*needle,u16_t len,u16_t off){for(unsigned i=off;i+len<=p->tot_len;i++)if(!memcmp(p->payload+i,needle,len))return i;return 0xffff;}
static void pbuf_remove_header(struct pbuf*p,unsigned n){assert(p->tot_len>=n);p->payload+=n;p->tot_len-=n;}
static void close_handle(void){if(tftp_state.handle){closes++;tftp_state.handle=NULL;}tftp_state.port=0;tftp_state.addr.addr=0;tftp_state.last_data=NULL;}
static void *open_cb(const char*n,const char*m,u8_t w){(void)n;(void)m;(void)w;opens++;return (void*)1;}
static int write_cb(void*h,struct pbuf*p){assert(h);writes++;return write_failure?-1:p->tot_len;}
static void error_cb(void*h,int e,const char*m,int n){(void)h;(void)e;(void)m;(void)n;}
static struct tftp_context ctx={open_cb,write_cb,error_cb};
'''
a=s.index('static struct {',s.index('static struct tftp_state tftp_state;'));b=s.index('} tftp_completed_write;',a)+len('} tftp_completed_write;');program+=s[a:b]+'\n'
program+=function('tftp_repeat_final_ack(const ip_addr_t *addr, u16_t port,\n                      const struct pbuf *p, u16_t block)')+'\n'+function('tftp_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)')
program+=r'''
static void packet(unsigned op,unsigned block,unsigned bytes,unsigned port){
 struct pbuf*p=calloc(1,sizeof *p);p->tot_len=bytes+4;p->allocation=calloc(1,p->tot_len);p->payload=p->allocation;
 u16_t header[2]={htons(op),htons(block)};memcpy(p->payload,header,4);ip_addr_t peer={42};tftp_recv(NULL,NULL,p,&peer,port);
}
static void begin(void){memset(&tftp_state,0,sizeof tftp_state);tftp_state.ctx=&ctx;tftp_state.handle=(void*)1;tftp_state.addr.addr=42;tftp_state.port=123;tftp_state.blknum=1;tftp_state.mode_write=1;tftp_state.tftp_mode=1;}
int main(void){
 begin();packet(TFTP_DATA,1,0,123);assert(writes==1&&closes==1&&last_ack==1&&!tftp_state.handle);
 // The final ACK is lost: repeat DATA must be acknowledged without writing.
 packet(TFTP_DATA,1,0,123);assert(writes==1&&closes==1&&acks==2&&last_ack==1);
 // Its dally cache must not block another live transfer.
 begin();tftp_state.port=124;packet(TFTP_DATA,1,0,123);assert(tftp_state.handle&&(tftp_state.port==124)&&writes==1&&acks==3);
 // Different source ports, block numbers and lengths never hit the cache.
 unsigned before=acks;packet(TFTP_DATA,2,0,123);packet(TFTP_DATA,1,1,123);packet(TFTP_DATA,1,0,125);assert(acks==before);
 now=20000;packet(TFTP_DATA,1,0,123);assert(acks==before);
 // Duplicate first request re-ACKs block 0 without opening/truncating.
 begin();packet(TFTP_WRQ,0,4,123);assert(last_ack==0&&!opens&&writes==1);
 // Write errors close once and must never enter the completion cache.
 write_failure=1;tftp_completed_write.valid=0;unsigned c=closes;
 packet(TFTP_DATA,1,0,123);assert(closes==c+1&&!tftp_completed_write.valid);
 // A short/truncated datagram is discarded before reading its header.
 struct pbuf*p=calloc(1,sizeof *p);p->tot_len=1;p->allocation=calloc(1,1);p->payload=p->allocation;ip_addr_t peer={42};tftp_recv(NULL,NULL,p,&peer,123);
 assert(freed==10);
 // A full DATA block stays active; only a subsequent short block completes.
 write_failure=0;tftp_completed_write.valid=0;begin();unsigned w=writes,cclose=closes;
 packet(TFTP_DATA,1,512,123);assert(writes==w+1&&tftp_state.handle&&tftp_state.blknum==2);
 packet(TFTP_DATA,2,1,123);assert(writes==w+2&&!tftp_state.handle&&closes==cclose+1);
 packet(TFTP_DATA,2,1,123);assert(writes==w+2&&closes==cclose+1&&last_ack==2);
 // Expiry comparisons remain correct across the millisecond counter wrap.
 tftp_completed_write.time=0xfffffff0u;now=32;before=acks;packet(TFTP_DATA,2,1,123);assert(acks==before+1);
 puts("PASS: actual TFTP receive handles lost final/initial ACKs, independent transfer slots, expiry, write errors and short packets");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'case.c').write_text(program);subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'case.c'),'-o',str(p/'check')],check=True);subprocess.run([str(p/'check')],check=True)
