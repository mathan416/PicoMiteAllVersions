#!/usr/bin/env python3
"""Exercise the real connection routine over failed and repeated associations."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'PicoMite.c').read_text();a=s.index('void WebConnect(void)');b=s.index('\n#endif',a);body=s[a:b]
code=r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#define CYW43_NO_POWERSAVE_MODE 0
#define CYW43_AUTH_WPA2_AES_PSK 1
#define CYW43_AUTH_OPEN 0
#define CYW43_ITF_STA 0
#define CYW43_DEFAULT_PM 0
#define STRINGSIZE 256
typedef int ip4_addr_t;
static struct {char SSID[16],PASSWORD[16],ipaddress[16],gateway[16],mask[16],hostname[16];int disabletftp,UDP_PORT;} Option;
static struct {void *netif;} cyw43_state;
static void *netif_list;
static int WIFIconnected,result,attempts,tcp,tftp,udp;
static char output[2048];
static void MMPrintString(const char*s){assert(strlen(output)+strlen(s)<sizeof output);strcat(output,s);}
static void PInt(int n){char s[32];snprintf(s,sizeof s,"%d",n);MMPrintString(s);}
static void PRet(void){MMPrintString("\r\n");}
static void cyw43_arch_enable_sta_mode(void){}
static void dhcp_stop(void*n){(void)n;}
static int ip4addr_aton(const char*s,ip4_addr_t*i){(void)s;*i=1;return 1;}
static void netif_set_addr(void*n,ip4_addr_t*a,ip4_addr_t*m,ip4_addr_t*g){(void)n;(void)a;(void)m;(void)g;}
static void netif_set_hostname(void*n,char*s){(void)n;(void)s;}
static void cyw43_wifi_pm(void*s,int p){(void)s;(void)p;}
static int cyw43_arch_wifi_connect_timeout_ms(char*s,char*p,int a,int t){assert(!strcmp(s,"saved") && !p && a==0 && t==30000);attempts++;return result;}
static int cyw43_wifi_link_status(void*s,int i){(void)s;(void)i;return result?-1:1;}
#define netif_ip4_addr(n) (n)
static char *ip4addr_ntoa(void*n){(void)n;return "10.0.2.107";}
static void open_tcp_server(void){tcp++;}
static void cmd_tftp_server_init(void){tftp++;}
static void open_udp_server(void){udp++;}
'''+body+r'''
int main(void){
 strcpy(Option.SSID,"saved");Option.UDP_PORT=1;result=-1;WebConnect();assert(!WIFIconnected&&attempts==1&&!tcp&&!tftp&&!udp);assert(strstr(output,"failed to connect: code -1")&&strstr(output,"WEB CONNECT"));
 result=0;output[0]=0;WebConnect();assert(WIFIconnected&&attempts==2&&tcp==1&&tftp==1&&udp==1);assert(strstr(output,"Connected 10.0.2.107"));
 result=-1;WebConnect();assert(!WIFIconnected&&attempts==3&&tcp==1);
 result=0;WebConnect();assert(WIFIconnected&&attempts==4&&tcp==1&&tftp==1&&udp==1);
 puts("PASS: failed connection preserves retry path, success is reported, and repeated associations reuse existing listeners");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'case.c').write_text(code);subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined',str(p/'case.c'),'-o',str(p/'check')],check=True);subprocess.run([str(p/'check')],check=True)
