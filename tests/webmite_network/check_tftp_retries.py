#!/usr/bin/env python3
"""Retry regression entry point; complete modules supersede the old extracted stubs."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("check_tftp_errors.py")), run_name="__main__")
