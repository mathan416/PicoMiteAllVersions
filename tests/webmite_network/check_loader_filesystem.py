#!/usr/bin/env python3
"""Exercise the real LittleFS implementation and SD geometry recovery."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
void *GetMemory(int n){return malloc(n);}
void FreeMemory(unsigned char *p){free(p);}
#include "lfs.h"
#define BLOCK 4096u
#define ACTUAL (3u*1024u*1024u)
static uint8_t media[ACTUAL], saved[ACTUAL];
static uint8_t readbuf[256],writebuf[256],lookbuf[256];
static int writes, enforce;
uint32_t loader_flash_size(void){return 4u*1024u*1024u-8192u;}
int loader_filesystem_recover(lfs_t *,struct lfs_config *,int);
static int rd(const struct lfs_config *c,lfs_block_t b,lfs_off_t o,void *p,lfs_size_t n){
 (void)c;size_t a=(size_t)b*BLOCK+o;if(a>sizeof media||n>sizeof media-a)return LFS_ERR_IO;
 memcpy(p,media+a,n);return 0;
}
static int prog(const struct lfs_config *c,lfs_block_t b,lfs_off_t o,const void *p,lfs_size_t n){
 if(enforce && c->block_count>ACTUAL/BLOCK)return LFS_ERR_IO;
 size_t a=(size_t)b*BLOCK+o;if(a>sizeof media||n>sizeof media-a)return LFS_ERR_IO;
 writes++;for(unsigned i=0;i<n;i++)media[a+i]&=((const uint8_t *)p)[i];return 0;
}
static int erase(const struct lfs_config *c,lfs_block_t b){
 if(enforce && c->block_count>ACTUAL/BLOCK)return LFS_ERR_IO;
 if(b>=ACTUAL/BLOCK)return LFS_ERR_IO;writes++;memset(media+b*BLOCK,255,BLOCK);return 0;
}
static int syncfs(const struct lfs_config *c){(void)c;return 0;}
int main(void){
 lfs_t fs={0};struct lfs_config c={.read=rd,.prog=prog,.erase=erase,.sync=syncfs,.read_size=1,.prog_size=256,.block_size=BLOCK,.block_count=ACTUAL/BLOCK,.block_cycles=500,.cache_size=256,.lookahead_size=256,.read_buffer=readbuf,.prog_buffer=writebuf,.lookahead_buffer=lookbuf};
 memset(media,255,sizeof media);
 /* Blank media can initialize; no operations may enter a failed mount. */
 int err=lfs_mount(&fs,&c);assert(err<0);lfs_file_t file;assert(lfs_file_open(&fs,&file,"x",LFS_O_RDONLY)==LFS_ERR_IO);
 assert(loader_filesystem_recover(&fs,&c,err)==0);assert(lfs_unmount(&fs)==0);
 /* Reproduce the oversized SD5 superblock and a small inline user file. */
 uint32_t start=loader_flash_size()-ACTUAL;
 c.block_count=(16u*1024u*1024u-8192u-start)/BLOCK;
 assert(!lfs_format(&fs,&c));assert(!lfs_mount(&fs,&c));
 assert(!lfs_file_open(&fs,&file,"keep.txt",LFS_O_WRONLY|LFS_O_CREAT));assert(lfs_file_write(&fs,&file,"precious",8)==8);assert(!lfs_file_close(&fs,&file));assert(!lfs_unmount(&fs));
 memcpy(saved,media,sizeof media);writes=0;enforce=1;c.block_count=ACTUAL/BLOCK;
 err=lfs_mount(&fs,&c);assert(err==LFS_ERR_INVAL);
 assert(loader_filesystem_recover(&fs,&c,err)==1);
 assert(!lfs_file_open(&fs,&file,"keep.txt",LFS_O_RDONLY));char out[8];assert(lfs_file_read(&fs,&file,out,8)==8);assert(!memcmp(out,"precious",8));assert(!lfs_file_close(&fs,&file));
 int opened=lfs_file_open(&fs,&file,"keep.txt",LFS_O_WRONLY);
 if(!opened){lfs_file_write(&fs,&file,"modified",8);assert(lfs_file_close(&fs,&file)<0);}
 assert(!writes && !memcmp(saved,media,sizeof media));assert(!lfs_unmount(&fs));
 /* A damaged nonblank volume must remain byte-for-byte untouched. */
 memset(media,0x37,sizeof media);memcpy(saved,media,sizeof media);c.block_count=ACTUAL/BLOCK;err=lfs_mount(&fs,&c);assert(err<0);
 assert(loader_filesystem_recover(&fs,&c,err)<0);assert(!writes && !memcmp(saved,media,sizeof media));
 assert(lfs_stat(&fs,"keep.txt",&(struct lfs_info){0})==LFS_ERR_IO);
 /* Erased roots alone are not proof that the whole filesystem is blank. */
 memset(media,255,sizeof media);media[sizeof media-1]=0x45;memcpy(saved,media,sizeof media);
 err=lfs_mount(&fs,&c);assert(err<0);assert(loader_filesystem_recover(&fs,&c,err)<0);
 assert(!writes && !memcmp(saved,media,sizeof media));
 puts("PASS: blank init; legacy read-only recovery; failed-mount API guard; corrupt-volume preservation");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'case.c').write_text(program)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-DPICOCALC_UF2LOADER=1','-fsanitize=address,undefined','-I',str(r),str(p/'case.c'),str(r/'LoaderFilesystem.c'),str(r/'lfs.c'),str(r/'lfs_util.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
