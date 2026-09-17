"""Interrupt an isolated probe connection and require automatic recovery."""
import socket,threading,tempfile,pathlib,subprocess,os,time,sys
if len(sys.argv) != 4:
 raise SystemExit('usage: probe-cleat-reconnect.py PROBE RUNTIME_ROOT SESSION_ID')
upstream=str(pathlib.Path(sys.argv[2])/'wh-image-baseline/socket')
with tempfile.TemporaryDirectory(prefix='wh-reconnect-',dir='/tmp') as d:
 path=pathlib.Path(d)/'wh-image-baseline';path.mkdir()
 listener=socket.socket(socket.AF_UNIX);listener.bind(str(path/'socket'));listener.listen()
 active=[];stopping=threading.Event();blocked=threading.Event()
 def relay(a,b):
  try:
   while data:=a.recv(65536): b.sendall(data)
  except OSError: pass
  for s in (a,b):
   try:s.shutdown(socket.SHUT_RDWR)
   except OSError:pass
   s.close()
 def serve():
  while not stopping.is_set():
   try:a,_=listener.accept()
   except OSError:return
   if blocked.is_set(): a.close();continue
   b=socket.socket(socket.AF_UNIX);b.connect(upstream);active.extend([a,b])
   threading.Thread(target=relay,args=(a,b),daemon=True).start()
   threading.Thread(target=relay,args=(b,a),daemon=True).start()
 threading.Thread(target=serve,daemon=True).start()
 env=dict(os.environ,CLEAT_RUNTIME_DIR=d,WH_EXPECT_RECONNECT='1')
 p=subprocess.Popen([sys.argv[1],'daemon','unused',sys.argv[3]],env=env)
 time.sleep(1);blocked.set()
 for s in list(active):
  try:s.shutdown(socket.SHUT_RDWR)
  except OSError:pass
 time.sleep(1);blocked.clear()
 code=p.wait(timeout=20);stopping.set();listener.close();raise SystemExit(code)
