import base64, hashlib, ipaddress, json, secrets, socket, subprocess, sys, threading, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT=Path(__file__).resolve().parent
PACKAGE=ROOT.parent/'AlisaVoice-Mini2-test'
sys.path[:0]=[str(PACKAGE),str(PACKAGE/'windows/vendor')]
from alisa_cli.state import load, defaults
from alisa_cli import winssh, install, autostart, runtime
import paramiko

PORT=8769
ORIGIN=f'http://127.0.0.1:{PORT}'
TOKEN=secrets.token_urlsafe(32)
LOCK=threading.Lock()
state={'phase':'idle','message':'Ожидается подключение.'}
pending={}

def publish(phase,message):
    global state
    state={'phase':phase,'message':message}

def hostkey(host,port):
    transport=paramiko.Transport(socket.create_connection((host,port),timeout=6))
    try:
        transport.start_client(timeout=8)
        key=transport.get_remote_server_key()
        return key.get_name()+' SHA256:'+base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')
    finally:transport.close()

def checked_host(data):
    host=str(ipaddress.ip_address(data.get('host','')))
    address=ipaddress.ip_address(host)
    if not address.is_private or address.is_loopback or address.is_link_local or address.is_multicast or address.is_unspecified: raise ValueError('Укажите локальный IP-адрес колонки.')
    port=data.get('port',2222)
    if type(port) is not int or not 1<=port<=65535:raise ValueError('Некорректный порт SSH.')
    return host,port

def task(fn):
    try:fn()
    except Exception:publish('failed','Шаг не выполнен. Проверьте подключение, пароль и состояние колонки; сохранённые данные не удалены.')
    finally:LOCK.release()

def launch(fn,message):
    if not LOCK.acquire(False):raise ValueError('Другой шаг ещё выполняется.')
    publish('running',message)
    threading.Thread(target=task,args=(fn,),daemon=True).start()

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*a):pass
    def respond(self,status,body,kind='application/json; charset=utf-8'):
        if isinstance(body,dict):body=json.dumps(body,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'");self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def allowed(self):return self.headers.get('Host')==f'127.0.0.1:{PORT}'
    def do_GET(self):
        if not self.allowed():return self.respond(403,{'error':'Недопустимый адрес'})
        if self.path=='/api/state':return self.respond(200,state)
        if self.path in ('/','/app.js','/style.css'):
            name={'/':'index.html','/app.js':'app.js','/style.css':'style.css'}[self.path];raw=(ROOT/name).read_bytes().replace(b'__TOKEN__',TOKEN.encode());return self.respond(200,raw,{'index.html':'text/html; charset=utf-8','app.js':'text/javascript; charset=utf-8','style.css':'text/css; charset=utf-8'}[name])
        return self.respond(404,{'error':'Не найдено'})
    def do_POST(self):
        if not self.allowed() or self.headers.get('Origin')!=ORIGIN or not secrets.compare_digest(self.headers.get('X-Panel-Token',''),TOKEN):return self.respond(403,{'error':'Запрос отклонён'})
        try:
            n=int(self.headers.get('Content-Length','0'))
            if n<0 or n>2048:raise ValueError('Неверный размер запроса.')
            data=json.loads(self.rfile.read(n) or b'{}')
            if not isinstance(data,dict):raise ValueError('Некорректные данные.')
            if self.path=='/api/fingerprint':
                host,port=checked_host(data)
                pending.clear()
                def inspect():
                    fp=hostkey(host,port);pending.clear();pending.update({'host':host,'port':port,'fingerprint':fp});publish('fingerprint',fp)
                launch(inspect,'Проверяем SSH колонки…')
            elif self.path=='/api/connect':
                host,port=checked_host(data);password=data.get('password')
                if not isinstance(password,str) or not password or len(password)>512:raise ValueError('Введите пароль SSH.')
                if pending.get('host')!=host or pending.get('port')!=port:raise ValueError('Сначала проверьте отпечаток SSH.')
                fp=pending.get('fingerprint')
                def connect():
                    if not secrets.compare_digest(hostkey(host,port),fp): raise RuntimeError('SSH host key changed')
                    c=load('native')
                    if c.get('host')!=host or c.get('port')!=port:c=defaults('native')
                    c.update(host=host,port=port,user='root',ssh_explicit=True)
                    winssh.trust(c,lambda h,p,found: secrets.compare_digest(found,fp))
                    winssh.install_key(c,password)
                    pending.clear();publish('connected','Подключение и права root проверены.')
                launch(connect,'Проверяем пароль и права root…')
            elif self.path=='/api/install':
                def do_install():
                    c=load('native');install.install(c,activate=True);autostart.set_enabled(c,True)
                    from alisa_cli.state import save
                    save(c);publish('installed','Установлено и запущено. Автозапуск включён.')
                launch(do_install,'Устанавливаем компоненты. Не закрывайте страницу…')
            elif self.path=='/api/login':
                def do_login():
                    c=load('native')
                    if c.get('mode')!='native' or not c.get('installed'):raise RuntimeError('not installed')
                    p=subprocess.run([sys.executable,'-B','-X','utf8',str(PACKAGE/'alisa.py'),'native','login'],stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=1100,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                    if p.returncode:raise RuntimeError('login failed')
                    publish('complete','Вход сохранён. Откройте чат на колонке и задайте вопрос.')
                launch(do_login,'Войдите в ChatGPT в открывшемся окне браузера…')
            elif self.path=='/api/current':
                def current():
                    c=load('native');r=runtime.status(c)
                    if not r.get('agent_connected'):raise RuntimeError('not running')
                    publish('installed','Установленная служба работает. Можно проверить вход или открыть чат на колонке.')
                launch(current,'Проверяем существующую установку…')
            else:return self.respond(404,{'error':'Не найдено'})
            return self.respond(202,state)
        except (ValueError,TypeError,json.JSONDecodeError) as e:return self.respond(400,{'error':str(e)})
        except Exception:return self.respond(500,{'error':'Не удалось выполнить шаг.'})

if __name__=='__main__':
    httpd=ThreadingHTTPServer(('127.0.0.1',PORT),Handler)
    webbrowser.open(ORIGIN)
    print('Мастер установки работает на '+ORIGIN,flush=True)
    httpd.serve_forever()
