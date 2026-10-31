#!/usr/bin/env python3
"""Bounded live TFTP checks. Run only after independently confirming SD9."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,socket,struct,time,uuid
p=argparse.ArgumentParser();p.add_argument('--host',required=True);p.add_argument('--confirmed-sd9',action='store_true',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--client',type=Path,default=Path(__file__).resolve().parents[3]/'Applications/pico.py');a=p.parse_args()
spec=importlib.util.spec_from_file_location('pico',a.client);client=importlib.util.module_from_spec(spec);spec.loader.exec_module(client)
a.output.mkdir(parents=True,exist_ok=True);ip=socket.gethostbyname(a.host);peer=(ip,69);tag=uuid.uuid4().hex[:8];records=[];files=[];started=time.monotonic()
def save():
 (a.output/'results.json').write_text(json.dumps({'host':ip,'firmware':'SD9 independently confirmed','records':records,'created_files':files,'elapsed_seconds':round(time.monotonic()-started,2)},indent=2)+'\n')
def own(label,drive='A:'):
 name=f'{drive}/e9-{tag}-{label}.bin';files.append(name);save();return name
health=own('health');target=own('data');btarget=own('data','B:');cancel_target=own('cancel');timeout_target=own('timeout')
def request(op,name,mode=b'octet'):return struct.pack('!H',op)+name.encode()+b'\0'+mode+b'\0'
def recv(s,timeout=3):
 s.settimeout(timeout)
 while True:
  data,sender=s.recvfrom(2048)
  if sender[0]==ip:return data,sender

def exchange(packet,timeout=3):
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.sendto(packet,peer);return recv(s,timeout)[0]
def error(packet,code):
 result=exchange(packet);assert len(result)>=4 and struct.unpack('!HH',result[:4])==(5,code),(result,code)
def healthy():
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
  s.sendto(request(1,health),peer);data,sender=recv(s);assert data==struct.pack('!HH',3,1)+b'ok\n',data
  ack=struct.pack('!HH',4,1);s.sendto(ack,sender)
  # This server confirms it closed the read when it rejects a repeated final ACK.
  for _ in range(4):
   time.sleep(.04);s.sendto(ack,sender);reply,_=recv(s)
   if reply[:4]==struct.pack('!HH',5,2):break
   assert reply==data,reply
  else:raise AssertionError('Final ACK did not release the read slot')
def record(name,fn):
 begin=time.monotonic()
 try:fn()
 except Exception as e:
  records.append({'case':name,'pass':False,'error':str(e)});save();raise
 records.append({'case':name,'pass':True,'seconds':round(time.monotonic()-begin,2)});save();print('PASS:',name,flush=True)
def put(remote,data):
 src=a.output/'payload.bin';src.write_bytes(data);client.transfer(ip,str(src),remote)
def putget(remote,data):
 put(remote,data);out=a.output/'readback.bin';client.transfer(ip,str(out),remote,True);assert out.read_bytes()==data
put(health,b'ok\n');record('Initial transfer and final-ACK cleanup',healthy)
def missing():
 for n in range(20):
  drive='A:' if n%2==0 else 'B:';error(request(1,f'{drive}/e9-{tag}-missing-{n}.bin'),1);healthy()
record('20 alternating A:/B: missing files followed by healthy reads',missing)
def malformed():
 cases=[(b'\0\1unterminated',4),(request(1,''),4),(request(2,'x'*64),4),(request(2,'A:/x',b'netascii'),4),(request(2,'Z:/x'),1),(request(2,f'A:/e9-{tag}-absent/x'),1),(request(2,'A:/bad\x01name'),1),(struct.pack('!HH',77,0),4),(struct.pack('!HH',4,1)+b'x',4),(struct.pack('!HH',3,1)+bytes(513),4)]
 for packet,code in cases:error(packet,code);healthy()
 for packet in [b'',b'\0',b'\0\1x']:
  try:exchange(packet,.35)
  except socket.timeout:pass
  else:raise AssertionError('Truncated packet unexpectedly accepted')
  healthy()
record('Malformed/oversized/unsupported requests and clean recovery',malformed)
def cancelled():
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
  wrq=request(2,cancel_target);s.sendto(wrq,peer);ack,remote=recv(s);assert ack==struct.pack('!HH',4,0),ack
  s.sendto(wrq,remote);ack,_=recv(s);assert ack==struct.pack('!HH',4,0),ack
  s.sendto(struct.pack('!HH',3,1)+bytes([0xcd])*512,remote);ack,_=recv(s);assert ack==struct.pack('!HH',4,1),ack
  error(request(1,health),2)
  # Deliberately non-NUL-terminated peer error exercises bounded callback handling.
  s.sendto(struct.pack('!HH',5,0)+b'cancel',remote)
  time.sleep(.15)
 healthy();out=a.output/'cancelled-readback.bin';client.transfer(ip,str(out),cancel_target,True);assert out.read_bytes()==bytes([0xcd])*512
record('Duplicate initial WRQ, competing peer and cancelled partial upload',cancelled)
def timeout():
 with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
  s.sendto(request(2,timeout_target),peer);ack,_=recv(s);assert ack==struct.pack('!HH',4,0),ack
 print('Waiting 14 seconds for abandoned-upload timeout...',flush=True);time.sleep(14);healthy()
record('Abandoned upload expires and releases its slot',timeout)
for size in [0,32,512,513,32768]:
 data=bytes((i*31+size)%256 for i in range(size));record(f'A: overwrite and exact readback {size} bytes',lambda data=data:putget(target,data));records[-1]['sha256']=hashlib.sha256(data).hexdigest();save()
record('B: exact 513-byte upload/download after failure probes',lambda:putget(btarget,bytes((i*7)%256 for i in range(513))))
record('Final healthy read after all error and overwrite cases',healthy)
print('All bounded SD9 hardware TFTP tests passed. New files are listed for explicit console cleanup.',flush=True)
