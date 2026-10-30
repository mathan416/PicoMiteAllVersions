#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MaxPcb 8
#define ERR_OK 0
#define ERR_ABRT -13
#define ERR_VAL -6
#define ERR_MEM -1
#define ERR_CONN -11
#define TCP_WRITE_FLAG_COPY 1
#define CONSOLE_RX_BUF_SIZE 1460
#define __not_in_flash_func(name) name
typedef int err_t;
typedef uint16_t u16_t;
struct pbuf { uint16_t len, tot_len; const uint8_t *payload; struct pbuf *next; };
struct tcp_pcb {
    int local_port, dead;
    void *arg;
    err_t (*sent)(void *, struct tcp_pcb *, u16_t);
    err_t (*recv)(void *, struct tcp_pcb *, struct pbuf *, err_t);
    void (*error)(void *, err_t);
};
static struct { int Telnet; } Option = {1};
static int TCP_PORT=80, WIFIconnected=1, startupcomplete=1;
static unsigned char ConsoleRxBuf[CONSOLE_RX_BUF_SIZE];
static int ConsoleRxBufHead,ConsoleRxBufTail,BreakKey=3,keyselect,MMAbort,Keycomplete;
static void *KeyInterrupt;
static unsigned writes,aborts,closes,force_close_failure,force_write_failure;
static int TCPreceived,OptionErrorSkip;
static void *CurrentLinePtr;
static const char httpheadersfail[]="HTTP/1.0 404\r\n\r\n";
static void *GetMemory(unsigned n) { return malloc(n); }
static void error(const char *message, ...) { assert(0); }
static void MMPrintString(const char *message) {}
static uint8_t write_copies[128][512];
static const uint8_t *write_data[128];
static unsigned write_lengths[128],write_flags[128];
static void live(struct tcp_pcb *pcb) { assert(pcb && !pcb->dead); }
static uint64_t time_us_64(void) { static uint64_t t; return ++t; }
static void (*poll_hook)(void);
static unsigned queue_busy;
static void cyw43_arch_poll(void) { if(poll_hook) poll_hook(); }
static void FreeMemorySafe(void **p) { free(*p); *p=NULL; }
static int tcp_sndqueuelen(struct tcp_pcb *pcb) { live(pcb); return queue_busy ? 7 : 0; }
static err_t tcp_write(struct tcp_pcb *pcb,const void *data,unsigned size,unsigned flags) {
    live(pcb); if(force_write_failure)return ERR_MEM; assert(writes<128 && size<=512);
    write_flags[writes]=flags; write_lengths[writes]=size;
    if(flags&TCP_WRITE_FLAG_COPY) {
        memcpy(write_copies[writes],data,size); write_data[writes]=write_copies[writes];
    } else write_data[writes]=data;
    writes++; return ERR_OK;
}
static void tcp_arg(struct tcp_pcb *pcb,void *arg) { live(pcb); pcb->arg=arg; }
static void tcp_sent(struct tcp_pcb *pcb,err_t (*cb)(void *,struct tcp_pcb *,u16_t)) { live(pcb); pcb->sent=cb; }
static void tcp_recv(struct tcp_pcb *pcb,err_t (*cb)(void *,struct tcp_pcb *,struct pbuf *,err_t)) { live(pcb); pcb->recv=cb; }
static void tcp_err(struct tcp_pcb *pcb,void (*cb)(void *,err_t)) { live(pcb); pcb->error=cb; }
static void tcp_poll(struct tcp_pcb *pcb,void *cb,unsigned interval) { live(pcb); }
static err_t tcp_close(struct tcp_pcb *pcb) {
    live(pcb); closes++;
    if(force_close_failure) return ERR_MEM;
    pcb->dead=1; return ERR_OK;
}
static void tcp_abort(struct tcp_pcb *pcb) { live(pcb); pcb->dead=1; aborts++; }
static void tcp_recved(struct tcp_pcb *pcb,unsigned size) { live(pcb); }
static void pbuf_free(struct pbuf *p) {}
static uint8_t pbuf_get_at(const struct pbuf *p,unsigned position) {
    while(p && position>=p->len) { position-=p->len; p=p->next; }
    assert(p); return p->payload[position];
}
#define HTTP_CALLBACKS(n) \
static err_t tcp_server_sent##n(void *a,struct tcp_pcb *p,u16_t l){return ERR_OK;} \
static err_t tcp_server_recv##n(void *a,struct tcp_pcb *p,struct pbuf *b,err_t e){return ERR_OK;}
HTTP_CALLBACKS(0) HTTP_CALLBACKS(1) HTTP_CALLBACKS(2) HTTP_CALLBACKS(3)
HTTP_CALLBACKS(4) HTTP_CALLBACKS(5) HTTP_CALLBACKS(6) HTTP_CALLBACKS(7)

static unsigned pbuf_copy_partial(struct pbuf *p,void *buffer,unsigned length,unsigned offset) {
    for(unsigned i=0;i<length;i++)((uint8_t *)buffer)[i]=pbuf_get_at(p,offset+i);
    return length;
}
