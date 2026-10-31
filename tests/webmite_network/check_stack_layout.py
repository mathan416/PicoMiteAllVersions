#!/usr/bin/env python3
"""Check the linked stack reservation rather than trusting compiler flags."""
import argparse, subprocess
p=argparse.ArgumentParser();p.add_argument("elf");p.add_argument("--nm",default="arm-none-eabi-nm");a=p.parse_args()
values={}
for line in subprocess.check_output([a.nm,a.elf],text=True).splitlines():
 parts=line.split()
 if len(parts)==3:
  try:values[parts[2]]=int(parts[0],16)
  except ValueError:pass
size=values["__StackTop"]-values["__StackBottom"]
assert size>=16384,f"Only {size} stack bytes reserved; expected at least 16384"
assert values["__HeapLimit"]<=values["__StackBottom"],"C heap overlaps stack"
assert values["__bss_end__"]<=values["__StackBottom"],"BSS overlaps stack"
assert values["__StackTop"]==0x20082000,"Unexpected Pico 2 W SRAM top"
print(f"PASS: {size} stack bytes; stack bottom 0x{values['__StackBottom']:08x}; heap and BSS do not overlap")
