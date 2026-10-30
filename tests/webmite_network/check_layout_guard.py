#!/usr/bin/env python3
"""Check that the actual linker assertion allows the boundary, rejects overlap."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
compiler=os.environ.get('ARM_CC','arm-none-eabi-gcc')
linker=os.environ.get('ARM_LD','arm-none-eabi-ld')
with tempfile.TemporaryDirectory(prefix='webmite-layout-') as temp:
    directory=Path(temp)
    (directory/'empty.c').write_text('void entry(void) {}\n')
    subprocess.run([compiler,'-mcpu=cortex-m33','-mthumb','-c',str(directory/'empty.c'),'-o',str(directory/'empty.o')],check=True)
    for end,allowed in [(0x1010dfff,True),(0x1010e000,True),(0x1010e001,False)]:
        result=subprocess.run([linker,'-T',str(ROOT/'webmite_layout_check.ld'),
            '--defsym=__flash_binary_end='+hex(end),str(directory/'empty.o'),'-o',str(directory/'test.elf')],capture_output=True,text=True)
        assert (result.returncode==0)==allowed,result.stdout+result.stderr
        if not allowed:assert 'overlaps saved options sector' in result.stderr
    print('PASS: linker guard accepts the reserved boundary and rejects overlap')
