#!/usr/bin/env python3
"""Exercise the actual timer-setup snippet with a loader-masked IRQ state."""
import pathlib, subprocess, tempfile
r=pathlib.Path(__file__).resolve().parents[2]
s=(r/"PicoMite.c").read_text();main=s.index("int MIPS16 main()")
a=s.index("    mSecTimer=time_us_64()/1000;",main)
b=s.index("    add_repeating_timer_us(-1000, timer_callback, NULL, &timer);",a)+len("    add_repeating_timer_us(-1000, timer_callback, NULL, &timer);")
snippet=s[a:b]
program=r'''#include <assert.h>
#include <stdint.h>
#include <stddef.h>
static int irqmask, restores, timer, ticks, countdown=16;
static uint64_t mSecTimer;
uint64_t time_us_64(void){return 1000000;}
void restore_interrupts(int mask){irqmask=mask;restores++;}
void timer_callback(void){ticks++;if(countdown)countdown--;}
int add_repeating_timer_us(int delay,void (*cb)(void),void *user,int *out){
 (void)user;(void)out;assert(delay==-1000);assert(!irqmask);
 for(int i=0;i<30;i++)cb();return 1;
}
int main(void){
#ifdef PICOCALC_UF2LOADER
 irqmask=1;
#else
 irqmask=0;
#endif
'''+snippet+r'''
 assert(!irqmask&&ticks==30&&countdown==0&&mSecTimer==1000);
#ifdef PICOCALC_UF2LOADER
 assert(restores==1);
#else
 assert(restores==0);
#endif
}
'''
with tempfile.TemporaryDirectory() as folder:
 d=pathlib.Path(folder);(d/"check.c").write_text(program)
 for mode in [[],["-DPICOCALC_UF2LOADER=1"]]:
  subprocess.run(["cc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",*mode,str(d/"check.c"),"-o",str(d/"check")],check=True)
  subprocess.run([str(d/"check")],check=True)
print("PASS: loader-masked startup enables IRQs before timer registration; USB path unchanged")
