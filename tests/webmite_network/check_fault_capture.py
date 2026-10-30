#!/usr/bin/env python3
"""Run the actual fault record/report code with mocked hardware registers."""
import pathlib, subprocess, tempfile
r=pathlib.Path(__file__).resolve().parents[2];s=(r/"FaultCapture.c").read_text()
def function(name):
 start=s.index(name);start=s.rfind("void ",0,start);level=0
 for end in range(s.index("{",start),len(s)):
  if s[end]=="{":level+=1
  if s[end]=="}":
   level-=1
   if not level:return s[start:end+1]
record=function("loader_fault_record");report=function("loader_report_previous_fault")
program=r'''#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define __not_in_flash_func(x) x
#define FAULT_MAGIC 0x50434654u
static uint32_t frame_data[64];
#define SRAM_BASE ((uintptr_t)frame_data)
#define SRAM_END ((uintptr_t)(frame_data+64))
struct {uint32_t cfsr;} scb,*m33_hw=&scb;
struct {uint32_t scratch[8];} wd,*watchdog_hw=&wd;
struct {int Autorun;} Option;
static jmp_buf reboot;
static int reports;
void watchdog_enable(int delay,bool pause){assert(delay==1&&!pause);longjmp(reboot,1);}
void tight_loop_contents(void){assert(!"reboot returned");}
void MMPrintString(const char *s){(void)s;reports++;}
void PIntH(uint32_t n){(void)n;}
void PRet(void){}
'''+record+"\n"+report+r'''
int main(void){
 frame_data[6]=0x10012345;
 if(!setjmp(reboot))loader_fault_record(frame_data,16);
 assert(wd.scratch[0]==FAULT_MAGIC&&wd.scratch[1]==0&&wd.scratch[3]==0x10012345);
 Option.Autorun=1;loader_report_previous_fault();assert(!wd.scratch[0]&&!Option.Autorun&&reports);
 int saved=reports;loader_report_previous_fault();assert(reports==saved);
 scb.cfsr=0x00100000;
 if(!setjmp(reboot))loader_fault_record((uint32_t *)1,16);
 assert(wd.scratch[1]==0x00100000&&wd.scratch[2]==1&&wd.scratch[3]==0);
 scb.cfsr=0;frame_data[24]=0x10056789;
 if(!setjmp(reboot))loader_fault_record(frame_data,0);
 assert(wd.scratch[3]==0x10056789);
 puts("PASS: fault registers survive reboot; incomplete stack frames are not read; report is once-only and disables autorun in RAM");
}
'''
with tempfile.TemporaryDirectory() as folder:
 d=pathlib.Path(folder);(d/"check.c").write_text(program)
 subprocess.run(["cc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",str(d/"check.c"),"-o",str(d/"check")],check=True)
 subprocess.run([str(d/"check")],check=True)
