#!/usr/bin/env python3
"""Run the actual network service loop across startup/disconnected states."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'PicoMite.c').read_text();a=s.index('void __not_in_flash_func(ProcessWeb)');brace=s.index('{',a);i=brace+1;depth=1
while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[a:i]
code=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define __not_in_flash_func(x) x
#define MaxPcb 2
#define CYW43_WL_GPIO_LED_PIN 0
struct tcp_pcb{int unused;};
typedef struct {struct tcp_pcb *client_pcb[2];uint64_t pcbopentime[2];int keepalive[2],buffer_recv[2],telnet_pcb_no;} TCP_SERVER_T;
static TCP_SERVER_T state;static void *TCPstate;
static int startupcomplete,WIFIconnected,polls;
static uint64_t now;
static struct {int ServerResponceTime,NoHeartbeat;} Option;
static uint64_t time_us_64(void){return now;}
static void cyw43_arch_poll(void){assert(startupcomplete);polls++;}
static bool cyw43_arch_gpio_get(int p){(void)p;return false;}
static void cyw43_arch_gpio_put(int p,int v){(void)p;(void)v;}
static void tcp_server_close(TCP_SERVER_T*s,int i){(void)s;(void)i;assert(!"idle test closed a client");}
static void error(const char*s,...){(void)s;assert(!"idle test raised error");}
static void TelnetPutC(int c,int f){(void)c;(void)f;}
'''+body+r'''
int main(void){
 Option.NoHeartbeat=1;state.telnet_pcb_no=99;
 ProcessWeb(1);assert(!polls); // No uninitialized driver use.
 startupcomplete=1;now=2000;ProcessWeb(1);assert(polls==1); // Failed association, no server.
 now+=2000;ProcessWeb(0);assert(polls==2); // Also services mode 0.
 WIFIconnected=1;TCPstate=NULL;now+=2000;ProcessWeb(1);assert(polls==3); // No TCP allocation yet.
 TCPstate=&state;now+=2000;ProcessWeb(1);assert(polls==4); // Normal idle connection.
 WIFIconnected=0;now+=2000;ProcessWeb(1);assert(polls==5); // Disconnected with old server.
 puts("PASS: actual ProcessWeb polls failed/down/no-server states, preserves connected service, and avoids uninitialized driver calls");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'case.c').write_text(code);subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-unused-but-set-variable','-fsanitize=address,undefined',str(p/'case.c'),'-o',str(p/'check')],check=True);subprocess.run([str(p/'check')],check=True)
