#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include "ff.h"
#include "diskio.h"
#include "lfs.h"
#define LWIP_UDP 1
#define LWIP_TFTP_MODE_SERVER 1
#define LWIP_TFTP_MODE_CLIENT 2
#define TFTP_MAX_FILENAME_LEN 63
#define TFTP_MAX_MODE_LEN 7
#define TFTP_TIMER_MSECS 1000
#define TFTP_TIMEOUT_MSECS 10000
#define TFTP_MAX_RETRIES 5
#define TFTP_PORT 69
#define ERR_OK 0
#define ERR_MEM (-1)
#define ERR_VAL (-6)
#define ERR_USE (-8)
#define IPADDR_TYPE_ANY 0
#define IP_ANY_TYPE NULL
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
#define MEMCPY memcpy
#define lwip_htons htons
#define lwip_ntohs ntohs
#define PP_HTONS htons
#define lwip_stricmp strcasecmp
#define LWIP_UNUSED_ARG(x) ((void)(x))
#define LWIP_ASSERT(s,x) assert(x)
#define LWIP_ERROR(s,x,handler) do {if(!(x)){handler;}}while(0)
#define LWIP_DEBUGF(a,b) ((void)0)
#define ip_addr_debug_print(a,b) ((void)0)
typedef uint8_t u8_t;typedef uint16_t u16_t;typedef uint32_t u32_t;typedef int err_t;
typedef struct {unsigned addr;} ip_addr_t;
#define ip_addr_eq(a,b) ((a)->addr==(b)->addr)
#define ip_addr_isany_val(a) (!(a).addr)
#define ip_addr_copy(a,b) ((a)=(b))
#define ip_addr_set_any(x,a) ((a)->addr=0)
struct pbuf {struct pbuf *next;void *payload;u16_t tot_len,len;void *allocation;};
struct udp_pcb {int unused;};
struct tftp_context {
 void *(*open)(const char*,const char*,u8_t);
 void (*close)(void*);
 int (*read)(void*,void*,int);
 int (*write)(void*,struct pbuf*);
 void (*error)(void*,int,const char*,int);
};
enum tftp_transfer_mode {TFTP_MODE_NETASCII,TFTP_MODE_OCTET,TFTP_MODE_BINARY};
struct pbuf *pbuf_alloc(int,u16_t,int);
u8_t pbuf_free(struct pbuf*);
void pbuf_realloc(struct pbuf*,u16_t);
err_t pbuf_copy(struct pbuf*,const struct pbuf*);
u16_t pbuf_copy_partial(const struct pbuf*,void*,u16_t,u16_t);
u16_t pbuf_memfind(const struct pbuf*,const void*,u16_t,u16_t);
struct udp_pcb *udp_new_ip_type(int);
err_t udp_bind(struct udp_pcb*,const ip_addr_t*,u16_t);
void udp_remove(struct udp_pcb*);
void udp_recv(struct udp_pcb*,void(*)(void*,struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t),void*);
err_t udp_sendto(struct udp_pcb*,struct pbuf*,const ip_addr_t*,u16_t);
void sys_timeout(u32_t,void(*)(void*),void*);
void sys_untimeout(void(*)(void*),void*);
u32_t sys_now(void);
err_t tftp_init_server(const struct tftp_context*);
extern int FatFSFileSystemSave;
extern lfs_t lfs;
extern struct lfs_config pico_lfs_cfg;
struct test_options {int SD_CS,CombinedCS,SYSTEM_MOSI,SD_MOSI_PIN,SYSTEM_MISO,SD_MISO_PIN,SYSTEM_CLK,SD_CLK_PIN;};
extern struct test_options Option;
static inline int IsInvalidPin(int pin){return pin<=0;}
