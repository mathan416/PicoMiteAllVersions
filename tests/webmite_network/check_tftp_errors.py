#!/usr/bin/env python3
"""Compile complete actual TFTP modules with real LittleFS and fault injection."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];tests=Path(__file__).parent
with tempfile.TemporaryDirectory() as folder:
 d=Path(folder)
 (d/'stub.h').write_text((tests/'tftp_error_stubs.h').read_text())
 for name in ['MMBasic_Includes.h','Hardware_Includes.h','lwip/apps/tftp_common.h','lwip/apps/tftp_server.h','lwip/apps/tftp_client.h','lwip/udp.h','lwip/timeouts.h','lwip/debug.h','lwip/sys.h','lwip/def.h']:
  p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stub.h"\n')
 for name in ['MMtftp.c','tftp.c']:(d/name).write_bytes((r/name).read_bytes())
 (d/'cases.c').write_text((tests/'tftp_error_cases.c').read_text())
 subprocess.run(['cc','-std=c11','-g','-Wall','-Wextra','-Werror','-DPICOCALC_UF2LOADER=1','-fsanitize=address,undefined','-I',str(d),'-I',str(r),str(d/'cases.c'),str(r/'lfs.c'),str(r/'lfs_util.c'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True)
