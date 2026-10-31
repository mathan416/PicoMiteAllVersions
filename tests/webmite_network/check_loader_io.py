#!/usr/bin/env python3
"""Run the actual LittleFS block callbacks against bounded flash mocks."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'FileIO.c').read_text();a=s.index('int __not_in_flash_func(fs_flash_read)');b=s.index('int __not_in_flash_func(fs_flash_sync)',a);callbacks=s[a:b]
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "lfs.h"
#define __not_in_flash_func(x) x
#undef TOP_OF_SYSTEM_FLASH
#undef RoundUpK4
#define TOP_OF_SYSTEM_FLASH 0x180000u
#define RoundUpK4(x) (x)
#define BLOCK_SIZE 4096u
#define XIP_BASE 0x10000000u
static struct {unsigned modbuff,modbuffsize;} Option;
static int reads,writes,erases;
uint32_t loader_flash_size(void){return 4u*1024u*1024u-8192u;}
int loader_flash_contains(uint32_t offset,size_t size){return offset<=loader_flash_size() && size<=loader_flash_size()-offset;}
void disable_interrupts_pico(void){}
void enable_interrupts_pico(void){}
void flash_range_program(uint32_t off,const void *data,size_t n){(void)data;assert(loader_flash_contains(off,n));writes++;}
void flash_range_erase(uint32_t off,size_t n){assert(loader_flash_contains(off,n));erases++;}
static void *mock_copy(void *out,const void *src,size_t n){uintptr_t off=(uintptr_t)src-XIP_BASE;assert(off<=UINT32_MAX && loader_flash_contains(off,n));reads++;return memset(out,0x42,n);}
#define memcpy mock_copy
'''+callbacks+r'''
int main(void){
 uint8_t data[512];struct lfs_config c={.read_size=1,.prog_size=256,.block_count=(loader_flash_size()-TOP_OF_SYSTEM_FLASH)/4096};
 assert(!fs_flash_read(&c,0,0,data,sizeof data) && reads==1);
 assert(!fs_flash_prog(&c,0,0,data,sizeof data) && writes==1);
 assert(!fs_flash_erase(&c,c.block_count-1) && erases==1);
 assert(fs_flash_read(&c,c.block_count,0,data,1)==LFS_ERR_IO);
 assert(fs_flash_read(&c,0,4095,data,2)==LFS_ERR_IO);
 assert(fs_flash_prog(&c,c.block_count,0,data,256)==LFS_ERR_IO);
 assert(fs_flash_erase(&c,c.block_count)==LFS_ERR_IO);
 c.block_count=(16u*1024u*1024u-8192u-TOP_OF_SYSTEM_FLASH)/4096;
 assert(!fs_flash_read(&c,0,0,data,32) && reads==2);
 assert(fs_flash_read(&c,c.block_count-1,0,data,32)==LFS_ERR_IO);
 assert(fs_flash_prog(&c,0,0,data,256)==LFS_ERR_IO);
 assert(fs_flash_erase(&c,0)==LFS_ERR_IO);
 assert(writes==1 && erases==1 && reads==2);
 puts("PASS: actual LittleFS callbacks bound translated reads/writes and prohibit legacy-geometry writes");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'case.c').write_text(program)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-DPICOCALC_UF2LOADER=1','-fsanitize=address,undefined','-I',str(r),str(p/'case.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
