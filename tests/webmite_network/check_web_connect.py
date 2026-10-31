#!/usr/bin/env python3
"""Exercise the actual WEB CONNECT branch including silent/no-IP cases."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'Custom.c').read_text();a=s.index('void cmd_web(void)');b=s.index('        tp=checkstring(cmdline, (unsigned char *)"SCAN");',a);branch=s[a:b]+'}\n'
program=r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define CYW43_ITF_STA 0
#define CYW43_LINK_UP 3
static unsigned char *cmdline;
static struct {char SSID[32];} Option;
static struct {void *netif;} cyw43_state;
static int WIFIconnected,status,reconnects,configs;
static char output[256];
static unsigned char *checkstring(unsigned char *s,unsigned char *word){(void)s;(void)word;return cmdline;}
static void setwifi(unsigned char *s){(void)s;configs++;}
static void WebConnect(void){reconnects++;}
static int cyw43_tcpip_link_status(void *s,int i){(void)s;(void)i;return status;}
#define netif_ip4_addr(x) (x)
static char *ip4addr_ntoa(void *n){(void)n;return "10.0.2.107";}
static void MMPrintString(const char*s){assert(strlen(output)+strlen(s)<sizeof output);strcat(output,s);}
static void PRet(void){MMPrintString("\r\n");}
'''+branch+r'''
int main(void){
 cmdline=(unsigned char *)"";cmd_web();assert(strstr(output,"not configured")&&!reconnects);
 strcpy(Option.SSID,"saved");output[0]=0;WIFIconnected=1;status=3;cmd_web();assert(strstr(output,"Already connected: 10.0.2.107")&&!reconnects);
 // Association alone is not sufficient: no IP must retry, even with stale flag.
 status=1;cmd_web();assert(reconnects==1);status=2;cmd_web();assert(reconnects==2);
 status=0;cmd_web();assert(reconnects==3);status=-1;cmd_web();assert(reconnects==4);
 WIFIconnected=0;status=3;cmd_web();assert(reconnects==5);
 cmdline=(unsigned char *)"credentials";cmd_web();assert(configs==1&&reconnects==6);
 puts("PASS: WEB CONNECT reports connected/unconfigured states and retries failed/down/no-IP states");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'case.c').write_text(program);subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'case.c'),'-o',str(p/'check')],check=True);subprocess.run([str(p/'check')],check=True)
