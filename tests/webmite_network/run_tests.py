#!/usr/bin/env python3
"""Compile actual network routines against lwIP stubs; no device or SDK required."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def function(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        if text[end] == '{': depth += 1
        elif text[end] == '}': depth -= 1
        end += 1
    return text[start:end] + '\n'

server = (ROOT / 'MMtcpserver.c').read_text()
telnet = (ROOT / 'MMtelnet.c').read_text()
header = (ROOT / 'Custom.h').read_text()
start = header.index('typedef struct TCP_SERVER_T_')
end = header.index('} TCP_SERVER_T;', start) + len('} TCP_SERVER_T;')
code = (Path(__file__).with_name('stubs.c')).read_text()
code += header[start:end] + '\n'
code += '''
static TCP_SERVER_T storage;
static TCP_SERVER_T *TCPstate = &storage;
static char Telnetbuff[256];
static int Telnetpos;
static unsigned char telnet_rx_state;
static int telnet_lastchar = -1;
static const uint8_t telnet_init_options[] = {255, 251, 3, 0};
static err_t starttelnet(struct tcp_pcb *, int, void *);
void TelnetPutC(int, int);
'''
for marker in ['void tcp_server_forget(', 'err_t tcp_server_close(',
               'static err_t tcp_server_result(', 'err_t tcp_server_send_data(', 'err_t tcp_server_recv(',
               'static void tcp_server_err(']:
    code += function(server, marker)
for number in range(8):
    code += function(server, 'static void tcp_server_err' + str(number) + '(')
for marker in ['static err_t tcp_telnet_sent(', 'void TelnetPutCommand(',
               'void __not_in_flash_func(TelnetPutC)', 'static void telnet_receive_char(',
               'err_t tcp_telnet_recv(', 'void tcp_telnet_err(', 'err_t starttelnet(']:
    text = function(telnet, marker)
    if marker == 'err_t starttelnet(': text = 'static ' + text
    code += text
code += function(server, 'static err_t tcp_server_accept(')
code += Path(__file__).with_name('cases.c').read_text()
with tempfile.TemporaryDirectory(prefix='webmite-network-') as directory:
    source = Path(directory) / 'network.c'
    binary = Path(directory) / 'network-tests'
    source.write_text(code)
    compiler = os.environ.get('CC', 'clang')
    subprocess.run([compiler, '-std=c11', '-g', '-O1', '-funsigned-char',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# Keep the CI host entry point covering the full TFTP protocol/file error suite.
subprocess.run([sys.executable, str(Path(__file__).with_name("check_tftp_errors.py"))], check=True)
