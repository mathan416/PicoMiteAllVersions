#!/usr/bin/env python3
"""Validate actual packed PicoCalc options and SaveOptions against a flash stub."""
from pathlib import Path
import os,subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[2]
release='--reproduce-release' in sys.argv

def contents(name):
    if release:return subprocess.check_output(['git','show','V6.00.03_PicoCalc:'+name],cwd=ROOT,text=True)
    return (ROOT/name).read_text()
header=contents('FileIO.h');a=header.index('struct option_s {');b=header.index('__attribute__((packed));',a)+len('__attribute__((packed));')
source=contents('FileIO.c');a2=source.index('void SaveOptions(void)');b2=source.index('{',a2)+1;depth=1
while depth:
    if source[b2]=='{':depth+=1
    elif source[b2]=='}':depth-=1
    b2+=1
code='''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define PICOMITEWEB 1
#define PICOCALC 1
#define GUICONTROLS 1
#define MAXKEYLEN 64
#define FLASH_PAGE_SIZE 256
#define FLASH_ERASE_SIZE 4096
#define FLASH_TARGET_OFFSET (1080*1024)
'''+header[a:b]+'''
static struct option_s Option;
static uint8_t flash[FLASH_ERASE_SIZE];
static unsigned programming_size;
static void uSec(unsigned n) {}
static void disable_interrupts_pico(void) {}
static void enable_interrupts_pico(void) {}
static void flash_range_erase(unsigned offset,unsigned size) {
    assert(offset==FLASH_TARGET_OFFSET && size==FLASH_ERASE_SIZE);
    memset(flash,0xff,sizeof(flash));
}
static void flash_range_program(unsigned offset,const uint8_t *data,unsigned size) {
    assert(offset==FLASH_TARGET_OFFSET && size<=FLASH_ERASE_SIZE);
    if(size%FLASH_PAGE_SIZE) {
        fprintf(stderr,"INVALID FLASH PROGRAM LENGTH: %u bytes\\n",size); exit(2);
    }
    assert(((uintptr_t)data&3)==0);
    programming_size=size; memcpy(flash,data,size);
}
'''+source[a2:b2]+'''
int main(void) {
    assert(sizeof(Option)==897);
    for(unsigned i=0;i<sizeof(Option);i++)((uint8_t *)&Option)[i]=(uint8_t)(i*31);
    SaveOptions();
    assert(programming_size==1024 && !memcmp(flash,&Option,sizeof(Option)));
    for(unsigned i=sizeof(Option);i<sizeof(flash);i++)assert(flash[i]==255);
    puts("PASS: all 897 option bytes preserved in an aligned 1024-byte record; padding is erased");
}
'''
with tempfile.TemporaryDirectory(prefix='webmite-options-') as d:
    c=Path(d)/'options.c';binary=Path(d)/'options';c.write_text(code)
    subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(binary)],check=True)
    result=subprocess.run([str(binary)],capture_output=True,text=True)
    print(result.stdout+result.stderr,end='')
    assert result.returncode==(2 if release else 0)
if not release:
    misc=contents('MM_Misc.c');a=misc.index('"DISK LOAD"');b=misc.index('"RESET"',a)
    assert 'SaveOptions();' in misc[a:b] and 'flash_range_program' not in misc[a:b]
    print('PASS: OPTION DISK LOAD uses the complete aligned options writer')
else:print('REPRODUCED: exact release passes the invalid 897-byte count to flash programming')
