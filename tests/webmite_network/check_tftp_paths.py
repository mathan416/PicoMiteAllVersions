#!/usr/bin/env python3
"""Exercise actual drive parsing with an empty interpreter command line."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'FileIO.c').read_text()
def function(marker):
 a=s.index(marker);brace=s.index('{',a);i=brace+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
code=r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <setjmp.h>
#define FF_MAX_LFN 256
#define FLASHFILE 1
#define FATFSFILE 2
static int FatFSFileSystem;
static char fullpathname[2][256];
static jmp_buf failed;
static void error(const char*s){(void)s;longjmp(failed,1);}
static void fullpath(char*q){if(q[1]==':')q+=2;snprintf(fullpathname[FatFSFileSystem],256,"%s",*q?q:"/");}
int drivecheck(char*,int*);
'''+function('int drivecheck(char *p, int *waste)')+'\n'+function('void getfullfilename(char *fname, char *q){')+r'''
static void check(int current,char *name,int expected,const char*path){char out[256]={0};FatFSFileSystem=current;getfullfilename(name,out);assert(FatFSFileSystem==expected);assert(!strcmp(out,path));}
int main(void){
 check(0,"B:/pico2-apps/test.uf2",1,"/pico2-apps/test.uf2");
 check(1,"A:/test.bin",0,"/test.bin");
 check(0,"b:/test.bin",1,"/test.bin");
 check(1,"/test.bin",1,"/test.bin");
 check(0,"test.bin",0,"/test.bin");
 if(!setjmp(failed)){char q[256]={0};getfullfilename("",q);assert(!"empty path accepted");}
 if(!setjmp(failed)){char q[256]={0};getfullfilename("B:",q);assert(!"empty drive-only path accepted");}
 puts("PASS: explicit A:/B: and relative paths resolve without a BASIC command; empty names fail safely");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'case.c').write_text(code);subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'case.c'),'-o',str(p/'check')],check=True);subprocess.run([str(p/'check')],check=True)
