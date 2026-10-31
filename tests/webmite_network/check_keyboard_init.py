#!/usr/bin/env python3
"""Test the real startup probe against a controller requiring a 16 ms gap."""
import pathlib, subprocess, tempfile
r=pathlib.Path(__file__).resolve().parents[2]
s=(r/"I2C.c").read_text()
start=s.index("void InitPicoCalcKeyboard(void)")
level=0;end=start
for end in range(s.index("{",start),len(s)):
    if s[end]=="{":level+=1
    if s[end]=="}":
        level-=1
        if not level:break
function=s[start:end+1]
case=r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int noI2C, writes, reads, fail_writes, fail_reads;
uint64_t now, requested;
void uSec(int us){now+=us;}
void CheckI2CKeyboard(int noerror,int read){
 assert(noerror==1);
 if(!read){writes++;requested=now;if(fail_writes){fail_writes--;noI2C=1;}}
 else {reads++;assert(now-requested>=16000);if(fail_reads){fail_reads--;noI2C=1;}}
}
'''+function+r'''
static void reset(void){noI2C=writes=reads=fail_writes=fail_reads=0;now=requested=0;}
int main(void){
 reset();InitPicoCalcKeyboard();assert(!noI2C&&writes==1&&reads==1&&now>=36000);
 reset();fail_writes=1;InitPicoCalcKeyboard();assert(!noI2C&&writes==2&&reads==1);
 reset();fail_reads=1;InitPicoCalcKeyboard();assert(!noI2C&&writes==2&&reads==2);
 reset();fail_writes=3;InitPicoCalcKeyboard();assert(noI2C&&writes==3&&reads==0);
 reset();fail_reads=3;InitPicoCalcKeyboard();assert(noI2C&&writes==3&&reads==3);
 puts("PASS: 16 ms startup read gap, transient write/read recovery, bounded permanent failure");
}
'''
# Request-only phase must return before the decode; original used uninitialized buff.
poll=s[s.index("void CheckI2CKeyboard(int noerror, int read)"):start]
assert "uint16_t buff = 0;" in poll
assert "return; // Request phase has no key data to decode." in poll
# Compile the real I2C1 transaction statements, not a reimplementation.
branch=s[s.index("    I2C2_Sendlen = 1;",s.index("void CheckI2CKeyboard")):s.index("  uSec(1000);",s.index("void CheckI2CKeyboard"))]
branch=branch.rsplit("  }",1)[0]
buscase=r'''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PICOCALC 1
int I2C2_Sendlen,I2C2_Rcvlen,I2C2_Status,reads,writes;
unsigned char I2C_Send_Buffer[256];
char *I2C2_Rcvbuf_String;
double *I2C2_Rcvbuf_Float;
int64_t *I2C2_Rcvbuf_Int;
uint64_t now,requested;
void uSec(int us){now+=us;}
int DoRtcI2C(int addr,unsigned char *buf){
 assert(addr==0x1f);
 if(!buf){assert(I2C2_Sendlen==1&&I2C2_Rcvlen==0&&I2C_Send_Buffer[0]==9);requested=now;writes++;}
 else{assert(I2C2_Sendlen==0&&I2C2_Rcvlen==2);assert(now-requested>=16000);uint16_t key=0x4101;memcpy(buf,&key,2);reads++;}
 return 1;
}
void transaction(void){uint16_t buff=0;
'''+branch+r'''
 assert(buff==0x4101);return;
i2c_error_exit:assert(!"I2C transaction failed");
}
int main(void){transaction();assert(reads==1&&writes==1);puts("PASS: actual PicoCalc I2C1 transaction waits 16 ms before key read");}
'''
with tempfile.TemporaryDirectory() as folder:
    d=pathlib.Path(folder);(d/"check.c").write_text(case)
    subprocess.run(["cc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",str(d/"check.c"),"-o",str(d/"check")],check=True)
    subprocess.run([str(d/"check")],check=True)

with tempfile.TemporaryDirectory() as folder:
    d=pathlib.Path(folder);(d/"bus.c").write_text(buscase)
    subprocess.run(["cc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",str(d/"bus.c"),"-o",str(d/"bus")],check=True)
    subprocess.run([str(d/"bus")],check=True)
