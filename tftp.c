/*
 *
 * @file tftp.c
 *
 * @author   Logan Gunthorpe <logang@deltatee.com>
 *           Dirk Ziegelmeier <dziegel@gmx.de>
 *
 * @brief    Trivial File Transfer Protocol (RFC 1350)
 *
 * Copyright (c) Deltatee Enterprises Ltd. 2013
 * All rights reserved.
 *
 */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification,are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Logan Gunthorpe <logang@deltatee.com>
 *         Dirk Ziegelmeier <dziegel@gmx.de>
 *
 */

/*
 * @defgroup tftp TFTP client/server
 * @ingroup apps
 *
 * This is simple TFTP client/server for the lwIP raw API.
 * You need to increase MEMP_NUM_SYS_TIMEOUT by one if you use TFTP!
 */

#include "lwip/apps/tftp_client.h"
#include "lwip/apps/tftp_server.h"

#if LWIP_UDP

#include "lwip/udp.h"
#include "lwip/timeouts.h"
#include "lwip/sys.h"
#include "lwip/debug.h"

#define TFTP_MAX_PAYLOAD_SIZE 512
#define TFTP_HEADER_LENGTH    4

#define TFTP_RRQ   1
#define TFTP_WRQ   2
#define TFTP_DATA  3
#define TFTP_ACK   4
#define TFTP_ERROR 5

enum tftp_error {
  TFTP_ERROR_FILE_NOT_FOUND    = 1,
  TFTP_ERROR_ACCESS_VIOLATION  = 2,
  TFTP_ERROR_DISK_FULL         = 3,
  TFTP_ERROR_ILLEGAL_OPERATION = 4,
  TFTP_ERROR_UNKNOWN_TRFR_ID   = 5,
  TFTP_ERROR_FILE_EXISTS       = 6,
  TFTP_ERROR_NO_SUCH_USER      = 7
};

#include <string.h>

struct tftp_state {
  const struct tftp_context *ctx;
  void *handle;
  struct pbuf *last_data;
  struct udp_pcb *upcb;
  ip_addr_t addr;
  u16_t port;
  u32_t timer;
  u32_t last_pkt;
  u16_t blknum;
  u8_t retries;
  u8_t mode_write;
  u8_t tftp_mode;
  char request_name[TFTP_MAX_FILENAME_LEN + 1];
};

static struct tftp_state tftp_state;

/* Remember the final received DATA after closing its file. If the ACK is
 * lost, RFC 1350 permits the sender to retransmit: acknowledge it again
 * without reopening, rewriting, or reserving the active-transfer slot. */
static struct {
  ip_addr_t addr;
  u16_t port, block, length;
  u32_t time;
  u8_t valid;
} tftp_completed_write;
static int tftp_repeat_final_ack(const ip_addr_t *addr, u16_t port,
                                 const struct pbuf *p, u16_t block);

static void tftp_tmr(void *arg);

static void
close_handle(void)
{
  tftp_state.port = 0;
  ip_addr_set_any(0, &tftp_state.addr);

  if (tftp_state.last_data != NULL) {
    pbuf_free(tftp_state.last_data);
    tftp_state.last_data = NULL;
  }

  sys_untimeout(tftp_tmr, NULL);

  if (tftp_state.handle) {
    void *handle = tftp_state.handle;
    tftp_state.handle = NULL;
    tftp_state.ctx->close(handle);
    LWIP_DEBUGF(TFTP_DEBUG | LWIP_DBG_STATE, ("tftp: closing\n"));
  }
}

static struct pbuf*
init_packet(u16_t opcode, u16_t extra, size_t size)
{
  struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(TFTP_HEADER_LENGTH + size), PBUF_RAM);
  u16_t* payload;

  if (p != NULL) {
    payload = (u16_t*) p->payload;
    payload[0] = PP_HTONS(opcode);
    payload[1] = lwip_htons(extra);
  }

  return p;
}

static err_t
send_request(const ip_addr_t *addr, u16_t port, u16_t opcode, const char* fname, const char* mode)
{
  size_t fname_length = strlen(fname)+1;
  size_t mode_length = strlen(mode)+1;
  struct pbuf* p = init_packet(opcode, 0, fname_length + mode_length - 2);
  char* payload;
  err_t ret;

  if (p == NULL) {
    return ERR_MEM;
  }

  payload = (char*) p->payload;
  MEMCPY(payload+2,              fname, fname_length);
  MEMCPY(payload+2+fname_length, mode,  mode_length);

  ret = udp_sendto(tftp_state.upcb, p, addr, port);
  pbuf_free(p);
  return ret;
}

static err_t
send_error(const ip_addr_t *addr, u16_t port, enum tftp_error code, const char *str)
{
  int str_length = strlen(str);
  struct pbuf *p;
  u16_t *payload;
  err_t ret;

  p = init_packet(TFTP_ERROR, code, str_length + 1);
  if (p == NULL) {
    return ERR_MEM;
  }

  payload = (u16_t *) p->payload;
  MEMCPY(&payload[2], str, str_length + 1);

  ret = udp_sendto(tftp_state.upcb, p, addr, port);
  pbuf_free(p);
  return ret;
}

static err_t
send_ack(const ip_addr_t *addr, u16_t port, u16_t blknum)
{
  struct pbuf *p;
  err_t ret;

  p = init_packet(TFTP_ACK, blknum, 0);
  if (p == NULL) {
    return ERR_MEM;
  }

  ret = udp_sendto(tftp_state.upcb, p, addr, port);
  pbuf_free(p);
  return ret;
}

static int
tftp_repeat_final_ack(const ip_addr_t *addr, u16_t port,
                      const struct pbuf *p, u16_t block)
{
  if (!tftp_completed_write.valid || port != tftp_completed_write.port ||
      !ip_addr_eq(addr, &tftp_completed_write.addr) ||
      block != tftp_completed_write.block ||
      p->tot_len != tftp_completed_write.length + TFTP_HEADER_LENGTH ||
      (u32_t)(sys_now() - tftp_completed_write.time) >= 2u*TFTP_TIMEOUT_MSECS)
    return 0;
  send_ack(addr, port, block);
  return 1;
}

static err_t
resend_data(const ip_addr_t *addr, u16_t port)
{
  err_t ret;
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, tftp_state.last_data->len, PBUF_RAM);
  if (p == NULL) {
    return ERR_MEM;
  }

  ret = pbuf_copy(p, tftp_state.last_data);
  if (ret != ERR_OK) {
    pbuf_free(p);
    return ret;
  }

  ret = udp_sendto(tftp_state.upcb, p, addr, port);
  pbuf_free(p);
  return ret;
}

static void
send_data(const ip_addr_t *addr, u16_t port)
{
  u16_t *payload;
  int ret;

  if (tftp_state.last_data != NULL) {
    pbuf_free(tftp_state.last_data);
  }

  tftp_state.last_data = init_packet(TFTP_DATA, tftp_state.blknum, TFTP_MAX_PAYLOAD_SIZE);
  if (tftp_state.last_data == NULL) {
    close_handle();
    return;
  }

  payload = (u16_t *) tftp_state.last_data->payload;

  ret = tftp_state.ctx->read(tftp_state.handle, &payload[2], TFTP_MAX_PAYLOAD_SIZE);
  if (ret < 0 || ret > TFTP_MAX_PAYLOAD_SIZE) {
    send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "Read failed");
    close_handle();
    return;
  }

  pbuf_realloc(tftp_state.last_data, (u16_t)(TFTP_HEADER_LENGTH + ret));
  resend_data(addr, port);
}

/* A stack-only view strips a possibly split header without modifying or
 * freeing any receive nodes. The original chain is always released once. */
static int tftp_payload_view(const struct pbuf *p, struct pbuf *view) {
  u16_t skip = TFTP_HEADER_LENGTH;
  u16_t remaining = p->tot_len - skip;
  const struct pbuf *q = p;
  while (q && skip >= q->len) { skip -= q->len; q = q->next; }
  memset(view, 0, sizeof *view);
  if (!q) return !skip && !remaining;
  *view = *q;
  view->payload = (u8_t *)q->payload + skip;
  view->len = q->len - skip;
  view->tot_len = remaining;
  return 1;
}

static void
tftp_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  u16_t header[2];
  LWIP_UNUSED_ARG(arg); LWIP_UNUSED_ARG(upcb);
  if (p->tot_len < sizeof header || pbuf_copy_partial(p, header, sizeof header, 0) != sizeof header) {
    pbuf_free(p); return;
  }
  u16_t opcode = lwip_ntohs(header[0]), block = lwip_ntohs(header[1]);
  if ((opcode == TFTP_DATA && p->tot_len > TFTP_MAX_PAYLOAD_SIZE + TFTP_HEADER_LENGTH) ||
      (opcode == TFTP_ACK && p->tot_len != TFTP_HEADER_LENGTH)) {
    send_error(addr, port, TFTP_ERROR_ILLEGAL_OPERATION, "Invalid packet");
    pbuf_free(p); return;
  }
  if (opcode == TFTP_DATA && tftp_repeat_final_ack(addr, port, p, block)) {
    pbuf_free(p); return;
  }
  if ((tftp_state.port && port != tftp_state.port) ||
      (!ip_addr_isany_val(tftp_state.addr) && !ip_addr_eq(&tftp_state.addr, addr))) {
    send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "Only one connection at a time is supported");
    pbuf_free(p); return;
  }
  switch (opcode) {
    case TFTP_RRQ: case TFTP_WRQ: {
      char filename[TFTP_MAX_FILENAME_LEN + 1], mode[TFTP_MAX_MODE_LEN + 1];
      const char zero = 0;
      u16_t name_end = pbuf_memfind(p, &zero, 1, 2);
      if (name_end == 0xffff || name_end < 3 || name_end - 2 > TFTP_MAX_FILENAME_LEN ||
          pbuf_copy_partial(p, filename, name_end - 1, 2) != name_end - 1) {
        send_error(addr, port, TFTP_ERROR_ILLEGAL_OPERATION, "Invalid filename"); break;
      }
      u16_t mode_end = pbuf_memfind(p, &zero, 1, name_end + 1);
      if (mode_end == 0xffff || mode_end <= name_end + 1 ||
          mode_end - name_end - 1 > TFTP_MAX_MODE_LEN || mode_end + 1 != p->tot_len ||
          pbuf_copy_partial(p, mode, mode_end - name_end, name_end + 1) != mode_end - name_end ||
          lwip_stricmp(mode, "octet")) {
        send_error(addr, port, TFTP_ERROR_ILLEGAL_OPERATION, "Octet mode required"); break;
      }
      if (tftp_state.handle) {
        if (tftp_state.blknum == 1 && !strcmp(filename, tftp_state.request_name) &&
            ((opcode == TFTP_WRQ) == (tftp_state.mode_write != 0))) {
          tftp_state.last_pkt = tftp_state.timer; tftp_state.retries = 0;
          if (tftp_state.mode_write) send_ack(addr, port, 0);
          else if (tftp_state.last_data) resend_data(addr, port);
        } else send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "Transfer busy");
        break;
      }
      if (!(tftp_state.tftp_mode & LWIP_TFTP_MODE_SERVER)) {
        send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "Server disabled"); break;
      }
      tftp_state.handle = tftp_state.ctx->open(filename, mode, opcode == TFTP_WRQ);
      if (!tftp_state.handle) {
        send_error(addr, port, TFTP_ERROR_FILE_NOT_FOUND, "Unable to open file"); break;
      }
      if (tftp_completed_write.valid && port == tftp_completed_write.port &&
          ip_addr_eq(addr, &tftp_completed_write.addr)) tftp_completed_write.valid = 0;
      strcpy(tftp_state.request_name, filename);
      tftp_state.blknum = 1; tftp_state.mode_write = opcode == TFTP_WRQ;
      ip_addr_copy(tftp_state.addr, *addr); tftp_state.port = port;
      tftp_state.last_pkt = tftp_state.timer; tftp_state.retries = 0;
      sys_untimeout(tftp_tmr, NULL);
      sys_timeout(TFTP_TIMER_MSECS, tftp_tmr, NULL);
      if (tftp_state.mode_write) send_ack(addr, port, 0);
      else send_data(addr, port);
      break;
    }
    case TFTP_DATA: {
      if (!tftp_state.handle || !tftp_state.mode_write) {
        send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "No write connection"); break;
      }
      if (block == tftp_state.blknum) {
        struct pbuf view;
        if (!tftp_payload_view(p, &view) || tftp_state.ctx->write(tftp_state.handle, &view) != (int)view.tot_len) {
          send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "Write failed"); close_handle(); break;
        }
        tftp_state.last_pkt = tftp_state.timer; tftp_state.retries = 0;
        if (view.tot_len < TFTP_MAX_PAYLOAD_SIZE) {
          ip_addr_copy(tftp_completed_write.addr, *addr);
          tftp_completed_write.port = port; tftp_completed_write.block = block;
          tftp_completed_write.length = view.tot_len; tftp_completed_write.time = sys_now();
          tftp_completed_write.valid = 1;
          close_handle();
        } else ++tftp_state.blknum;
        send_ack(addr, port, block);
      } else if ((u16_t)(block + 1) == tftp_state.blknum) send_ack(addr, port, block);
      else send_error(addr, port, TFTP_ERROR_UNKNOWN_TRFR_ID, "Wrong block");
      break;
    }
    case TFTP_ACK:
      if (!tftp_state.handle || tftp_state.mode_write) {
        send_error(addr, port, TFTP_ERROR_ACCESS_VIOLATION, "No read connection"); break;
      }
      /* Ignore duplicate ACKs: retransmission belongs to the timeout, not
       * the peer's duplicate ACK (avoid the Sorcerer's Apprentice loop). */
      if ((u16_t)(block + 1) == tftp_state.blknum) break;
      if (block != tftp_state.blknum) {
        send_error(addr, port, TFTP_ERROR_UNKNOWN_TRFR_ID, "Wrong block"); break;
      }
      tftp_state.last_pkt = tftp_state.timer; tftp_state.retries = 0;
      if (tftp_state.last_data && tftp_state.last_data->tot_len < TFTP_MAX_PAYLOAD_SIZE + TFTP_HEADER_LENGTH)
        close_handle();
      else { ++tftp_state.blknum; send_data(addr, port); }
      break;
    case TFTP_ERROR: {
      struct pbuf view;
      if (tftp_state.handle && tftp_payload_view(p, &view)) {
        tftp_state.ctx->error(tftp_state.handle, block, view.payload, view.len);
        close_handle();
      }
      break;
    }
    default: send_error(addr, port, TFTP_ERROR_ILLEGAL_OPERATION, "Unknown operation"); break;
  }
  pbuf_free(p);
}

static void
tftp_tmr(void *arg)
{
  LWIP_UNUSED_ARG(arg);

  tftp_state.timer++;

  if (tftp_state.handle == NULL) {
    return;
  }

  sys_timeout(TFTP_TIMER_MSECS, tftp_tmr, NULL);

  if ((tftp_state.timer - tftp_state.last_pkt) > (TFTP_TIMEOUT_MSECS / TFTP_TIMER_MSECS)) {
    if ((tftp_state.last_data != NULL) && (tftp_state.retries < TFTP_MAX_RETRIES)) {
      LWIP_DEBUGF(TFTP_DEBUG | LWIP_DBG_STATE, ("tftp: timeout, retrying\n"));
      resend_data(&tftp_state.addr, tftp_state.port);
      tftp_state.retries++;
    } else {
      LWIP_DEBUGF(TFTP_DEBUG | LWIP_DBG_STATE, ("tftp: timeout\n"));
      close_handle();
    }
  }
}

/*
 * Initialize TFTP client/server.
 * @param mode TFTP mode (client/server)
 * @param ctx TFTP callback struct
 */
err_t
tftp_init_common(u8_t mode, const struct tftp_context *ctx)
{
  err_t ret;

  /* Reinitializing a bound server would leak its old PCB and file state. */
  if (tftp_state.upcb) return ERR_USE;
  /* LWIP_ASSERT_CORE_LOCKED(); is checked by udp_new() */
  struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
  if (pcb == NULL) {
    return ERR_MEM;
  }

  ret = udp_bind(pcb, IP_ANY_TYPE, TFTP_PORT);
  if (ret != ERR_OK) {
    udp_remove(pcb);
    return ret;
  }

  tftp_completed_write.valid = 0;
  tftp_state.handle    = NULL;
  tftp_state.port      = 0;
  tftp_state.ctx       = ctx;
  tftp_state.timer     = 0;
  tftp_state.last_data = NULL;
  tftp_state.upcb      = pcb;
  tftp_state.tftp_mode = mode;

  udp_recv(pcb, tftp_recv, NULL);
  return ERR_OK;
}

/* @ingroup tftp
 * Initialize TFTP server.
 * @param ctx TFTP callback struct
 */
err_t
tftp_init_server(const struct tftp_context *ctx)
{
  return tftp_init_common(LWIP_TFTP_MODE_SERVER, ctx);
}

/* @ingroup tftp
 * Initialize TFTP client.
 * @param ctx TFTP callback struct
 */
err_t
tftp_init_client(const struct tftp_context *ctx)
{
  return tftp_init_common(LWIP_TFTP_MODE_CLIENT, ctx);
}

/* @ingroup tftp
 * Deinitialize ("turn off") TFTP client/server.
 */
void tftp_cleanup(void)
{
  LWIP_ASSERT("Cleanup called on non-initialized TFTP", tftp_state.upcb != NULL);
  udp_remove(tftp_state.upcb);
  close_handle();
  memset(&tftp_state, 0, sizeof(tftp_state));
}

static const char *
mode_to_string(enum tftp_transfer_mode mode)
{
  if (mode == TFTP_MODE_OCTET) {
    return "octet";
  }
  if (mode == TFTP_MODE_NETASCII) {
    return "netascii";
  }
  if (mode == TFTP_MODE_BINARY) {
    return "binary";
  }
  return NULL;
}

err_t
tftp_get(void* handle, const ip_addr_t *addr, u16_t port, const char* fname, enum tftp_transfer_mode mode)
{
  LWIP_ERROR("TFTP client is not enabled (tftp_init)", (tftp_state.tftp_mode & LWIP_TFTP_MODE_CLIENT) != 0, return ERR_VAL);
  LWIP_ERROR("tftp_get: invalid file name", fname != NULL, return ERR_VAL);
  LWIP_ERROR("tftp_get: invalid mode", mode <= TFTP_MODE_BINARY, return ERR_VAL);

  tftp_state.handle = handle;
  tftp_state.blknum = 1;
  tftp_state.mode_write = 1; /* We want to receive data */
  return send_request(addr, port, TFTP_RRQ, fname, mode_to_string(mode));
}

err_t
tftp_put(void* handle, const ip_addr_t *addr, u16_t port, const char* fname, enum tftp_transfer_mode mode)
{
  LWIP_ERROR("TFTP client is not enabled (tftp_init)", (tftp_state.tftp_mode & LWIP_TFTP_MODE_CLIENT) != 0, return ERR_VAL);
  LWIP_ERROR("tftp_put: invalid file name", fname != NULL, return ERR_VAL);
  LWIP_ERROR("tftp_put: invalid mode", mode <= TFTP_MODE_BINARY, return ERR_VAL);

  tftp_state.handle = handle;
  tftp_state.blknum = 1;
  tftp_state.mode_write = 0; /* We want to send data */
  return send_request(addr, port, TFTP_WRQ, fname, mode_to_string(mode));
}

#endif /* LWIP_UDP */
