#!/usr/bin/env python3
"""Compile and execute the actual TCP-open status formatter with strict checks."""
import pathlib, re, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "MMTCPclient.c").read_text()
function = source.split("static bool tcp_client_open(void *arg)", 1)[1]
statement = re.search(r"^[ \t]*(?:snprintf|sprintf)\(.*?;", function, re.M).group().strip()
program = r'''#include <stdio.h>
#include <string.h>
#include <assert.h>
struct state { int remote_addr; int TCP_PORT; };
static char *ip4addr_ntoa(const int *unused) { (void)unused; return "10.0.2.132"; }
int main(void) {
  char buff[256] = {0};
  struct state value = {0, 8123}, *state = &value;
''' + statement + r'''
  assert(strcmp(buff, "Connecting to 10.0.2.132 port 8123\r\n") == 0);
  return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    src = pathlib.Path(directory) / "status.c"
    exe = pathlib.Path(directory) / "status"
    src.write_text(program)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-Wformat=2", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("TCP connection status formatting: passed")
