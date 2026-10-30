#!/usr/bin/env python3
"""Exercise the actual loader flash shim against a translated mock ROM."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[2]
header = r'''#include <stdint.h>
#include <stddef.h>
#define PICO_RP2350 1
#define PICO_FLASH_SIZE_BYTES (4u*1024u*1024u)
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
#define XIP_BASE 0x10000000u
#define QMI_ATRANS0_BASE_BITS 0xfffu
#define QMI_ATRANS0_BASE_LSB 0
#define PICO_OK 0
#define CFLASH_OP_VALUE_ERASE 0
#define CFLASH_OP_VALUE_PROGRAM 1
#define CFLASH_OP_LSB 0
#define CFLASH_SECLEVEL_VALUE_SECURE 1
#define CFLASH_SECLEVEL_LSB 8
#define CFLASH_ASPACE_VALUE_RUNTIME 1
#define CFLASH_ASPACE_LSB 16
typedef struct {uint32_t flags;} cflash_flags_t;
struct mock_qmi {uint32_t atrans[8];};
extern struct mock_qmi *qmi_hw;
void panic(const char *, ...);
int rom_flash_op(cflash_flags_t, uintptr_t, uint32_t, uint8_t *);
void rom_flash_flush_cache(void);
'''
case = r'''#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "stub.h"
#include "LoaderFlash.h"
static struct mock_qmi mapping;
struct mock_qmi *qmi_hw=&mapping;
static unsigned char flash[PICO_FLASH_SIZE_BYTES];
static jmp_buf failure;
static int rom_error, calls, flushes;
void SDTrace(const char *text){(void)text;}
void panic(const char *msg, ...) {(void)msg; longjmp(failure,1);}
int rom_flash_op(cflash_flags_t flags, uintptr_t addr, uint32_t count, uint8_t *buf) {
  assert((flags.flags & (1u << CFLASH_ASPACE_LSB)) != 0);
  assert((flags.flags & (1u << CFLASH_SECLEVEL_LSB)) != 0);
  calls++;
  if(rom_error)return rom_error;
  uint32_t offset=(uint32_t)(addr-XIP_BASE)+mapping.atrans[0]*FLASH_SECTOR_SIZE;
  assert(offset+count<=sizeof flash);
  if(flags.flags & 1)memcpy(flash+offset,buf,count);
  else memset(flash+offset,255,count);
  return 0;
}
void rom_flash_flush_cache(void){flushes++;}
#define REJECT(code) do {int before=calls; if(!setjmp(failure)){code;assert(!"invalid operation accepted");} assert(calls==before);}while(0)
int main(void){
  uint8_t data[1024];memset(data,0x39,sizeof data);memset(flash,0xa5,sizeof flash);
  assert(loader_flash_size()==sizeof flash);
  mapping.atrans[0]=2;
  uint32_t capacity=loader_flash_size();assert(capacity==sizeof flash-8192);
  loader_flash_erase(0x10e000,4096);
  loader_flash_program(0x10e000,data,sizeof data);
  assert(!memcmp(flash+8192+0x10e000,data,sizeof data));
  for(int i=0;i<8192;i++)assert(flash[i]==0xa5);
  assert(flash[8192+0x10e000+1024]==255);
  assert(flash[8192+0x10e000+4096]==0xa5);
  loader_flash_program(capacity-256,data,256);
  assert(!memcmp(flash+sizeof flash-256,data,256));
  assert(flushes==calls);
  REJECT(loader_flash_program(capacity,data,256));
  REJECT(loader_flash_program(0,data,897));
  REJECT(loader_flash_program(1,data,256));
  REJECT(loader_flash_erase(0,4097));
  REJECT(loader_flash_erase(1,4096));
  REJECT(loader_flash_erase(0,0));
  REJECT(loader_flash_erase(0,(size_t)-4096));
  int before=flushes;rom_error=-4;
  if(!setjmp(failure)){loader_flash_erase(0,4096);assert(!"ROM failure ignored");}
  assert(flushes==before);
  puts("PASS: runtime translation preserves loader; exact aligned writes, bounds, overflow, ROM errors, cache flush");
}
'''
with tempfile.TemporaryDirectory() as folder:
    d=pathlib.Path(folder)
    (d/"stub.h").write_text("#pragma once\n"+header)
    for name in ["pico/stdlib.h","pico/bootrom.h","hardware/flash.h","hardware/structs/qmi.h"]:
        p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stub.h"\n')
    (d/"case.c").write_text(case)
    subprocess.run(["cc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I",str(d),"-I",str(root),str(root/"LoaderFlash.c"),str(d/"case.c"),"-o",str(d/"check")],check=True)
    subprocess.run([str(d/"check")],check=True)
