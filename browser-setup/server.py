import base64, hashlib, ipaddress, json, secrets, socket, subprocess, sys, threading, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT=Path(__file__).resolve().parent
PACKAGE=ROOT.parent/'AlisaVoice-Mini2-test'
sys.path[:0]=[str(PACKAGE),str(PACKAGE/'windows/vendor')]
from alisa_cli.state import load, defaults
from alisa_cli import winssh, install, autostart, runtime, vpn
import paramiko

PORT=8769
ORIGIN=f'http://127.0.0.1:{PORT}'
TOKEN=secrets.token_urlsafe(32)
LOCK=threading.Lock()
state={'phase':'idle','message':'Ожидается подключение.'}
pending={}
steps={'connected':False,'installed':False,'vpn':False,'login':False}

def publish(phase,message,panel=None):
    global state
    state={'phase':phase,'message':message, 'steps':dict(steps)}
    if panel:state['panel']=panel

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
    except Exception as error:
        from alisa_cli.errors import PublicError
        if isinstance(error,PublicError) and error.code=='ssh_auth':publish('failed','Колонка отклонила SSH-доступ. Проверьте пароль или приватный ключ. Если на компьютере уже есть рабочий ключ, выберите его в мастере.')
        elif isinstance(error,PublicError):publish('failed',str(error))
        else:publish('failed','Шаг не выполнен. Проверьте подключение и данные SSH; сохранённые данные не удалены.')
    finally:LOCK.release()

def launch(fn,message):
    if not LOCK.acquire(False):raise ValueError('Другой шаг ещё выполняется.')
    publish('running',message)
    threading.Thread(target=task,args=(fn,),daemon=True).start()

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*a):pass
    def respond(self,status,body,kind='application/json; charset=utf-8'):
        if isinstance(body,dict):body=json.dumps(body,ensure_ascii=False).encode()
        self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.send_header('Content-Security-Policy',f"default-src 'self'; script-src 'self'; style-src 'self' 'nonce-{TOKEN}'; frame-ancestors 'none'; base-uri 'none'");self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    def allowed(self):return self.headers.get('Host')==f'127.0.0.1:{PORT}'
    def do_GET(self):
        if not self.allowed():return self.respond(403,{'error':'Недопустимый адрес'})
        if self.path=='/api/state':return self.respond(200,{**state,'steps':dict(steps)})
        if self.path in ('/','/app.js','/style.css'):
            name={'/':'index.html','/app.js':'app.js','/style.css':'style.css'}[self.path];raw=(ROOT/name).read_bytes().replace(b'__TOKEN__',TOKEN.encode());return self.respond(200,raw,{'index.html':'text/html; charset=utf-8','app.js':'text/javascript; charset=utf-8','style.css':'text/css; charset=utf-8'}[name])
        return self.respond(404,{'error':'Не найдено'})
    def do_POST(self):
        if not self.allowed() or self.headers.get('Origin')!=ORIGIN or not secrets.compare_digest(self.headers.get('X-Panel-Token',''),TOKEN):return self.respond(403,{'error':'Запрос отклонён'})
        try:
            n=int(self.headers.get('Content-Length','0'))
            if n<0 or n>32768:raise ValueError('Неверный размер запроса.')
            data=json.loads(self.rfile.read(n) or b'{}')
            if not isinstance(data,dict):raise ValueError('Некорректные данные.')
            if self.path=='/api/fingerprint':
                host,port=checked_host(data)
                pending.clear()
                def inspect():
                    fp=hostkey(host,port);pending.clear();pending.update({'host':host,'port':port,'fingerprint':fp});publish('fingerprint',fp)
                launch(inspect,'Проверяем SSH колонки…')
            elif self.path=='/api/connect':
                host,port=checked_host(data);password=data.get('password','');private_key=data.get('private_key','');existing=data.get('existing',False);blank=data.get('blank',False)
                if not isinstance(password,str) or not isinstance(private_key,str) or len(password)>512 or len(private_key)>20000:raise ValueError('Проверьте данные SSH.')
                if type(existing) is not bool or type(blank) is not bool or sum((bool(password),bool(private_key),existing,blank))!=1:raise ValueError('Выберите один способ входа: пароль, пустой пароль, приватный ключ или сохранённый ключ.')
                if private_key and 'PRIVATE KEY-----' not in private_key:raise ValueError('Для входа нужен приватный ключ, а не публичный.')
                if pending.get('host')!=host or pending.get('port')!=port:raise ValueError('Сначала проверьте отпечаток SSH.')
                fp=pending.get('fingerprint')
                def connect():
                    if not secrets.compare_digest(hostkey(host,port),fp): raise RuntimeError('SSH host key changed')
                    c=load('native')
                    if c.get('host')!=host or c.get('port')!=port:c=defaults('native')
                    c.update(host=host,port=port,user='root',ssh_explicit=True)
                    winssh.trust(c,lambda h,p,found: secrets.compare_digest(found,fp))
                    if existing:
                        winssh.forget_password(c)
                        if not c.get('identity'):raise ValueError('Сохранённый ключ для этого адреса не найден.')
                        if winssh.run(c,'id -u').stdout.strip()!=b'0':raise ValueError('SSH-ключ не даёт права root.')
                    elif private_key:
                        winssh.forget_password(c)
                        from alisa_cli.state import HOME,write_private,save
                        import uuid
                        imported=HOME/'ssh'/('import-'+uuid.uuid4().hex)
                        try:
                            write_private(imported,(private_key.strip()+'\n').encode('utf-8'))
                            c['identity']=str(imported)
                            if winssh.run(c,'id -u').stdout.strip()!=b'0':raise ValueError('SSH-ключ не даёт права root.')
                            save(c)
                        except BaseException:
                            imported.unlink(missing_ok=True)
                            raise
                    else:
                        from alisa_cli.state import save
                        password_value='' if blank else password
                        if winssh.run(c,'id -u',password=password_value).stdout.strip()!=b'0':raise ValueError('SSH не даёт права root.')
                        winssh.remember_password(c,password_value)
                        save(c)
                    pending.clear();steps.update(connected=True,installed=False,vpn=False,login=False)
                    publish('connected','Подключение и права root проверены. Нажмите «Установить» — служба будет обновлена безопасно.')
                launch(connect,'Проверяем пароль и права root…')
            elif self.path=='/api/install':
                if not steps['connected']:raise ValueError('Сначала подключите колонку.')
                def do_install():
                    c=load('native')
                    install.install(c,activate=True);autostart.set_enabled(c,True)
                    from alisa_cli.state import save
                    save(c);current=runtime.status(c)
                    if not current.get('agent_connected'):raise RuntimeError('service not ready')
                    steps.update(installed=True,vpn=bool(c.get('vpn',{}).get('enabled')),login=bool(current.get('login_saved')))
                    panel=runtime.panel_url(c)
                    publish('installed','Установлено и запущено. Автозапуск включён. Откройте панель с телефона.',panel)
                launch(do_install,'Обновляем службу на колонке. Не закрывайте страницу…')
            elif self.path=='/api/vless':
                if not steps['installed']:raise ValueError('Сначала установите программу на колонку.')
                source=data.get('source','')
                if not isinstance(source,str) or not source.strip().startswith('vless://'):raise ValueError('Вставьте полный ключ VLESS, начинающийся с vless://')
                def configure_vless():
                    c=load('native');result=vpn.configure(c,source.strip())
                    c['vpn']['autostart']=bool(c.get('autostart'))
                    autostart.station_flag(c,'vpn-autostart',c['vpn']['autostart'])
                    from alisa_cli.state import save
                    save(c);steps['vpn']=True
                    publish('vpn','VLESS подключён и сохранён для GPT.')
                launch(configure_vless,'Проверяем и подключаем VLESS…')
            elif self.path=='/api/login':
                if not steps['installed']:raise ValueError('Сначала установите программу на колонку.')
                change=bool(steps['login'])
                def do_login():
                    c=load('native')
                    if c.get('mode')!='native' or not c.get('installed'):raise RuntimeError('not installed')
                    command=[sys.executable,'-B','-X','utf8',str(PACKAGE/'alisa.py'),'native','login']
                    if change:command.append('change')
                    p=subprocess.run(command,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=1100,env=winssh.password_environment(c),creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                    if p.returncode:raise RuntimeError('login failed')
                    steps['login']=True
                    publish('complete','Вход сохранён. Откройте чат на колонке и задайте вопрос.')
                launch(do_login,'Войдите в ChatGPT в открывшемся окне браузера…')
            elif self.path=='/api/panel-reset':
                if not steps['connected']:raise ValueError('Сначала подключите колонку.')
                def reset_panel():
                    c=load('native');panel=runtime.panel_url(c,reset=True)
                    publish('installed','Создан новый код подключения к панели.',panel)
                launch(reset_panel,'Создаём новый код панели…')
            elif self.path=='/api/current':
                def current():
                    c=load('native');r=runtime.status(c)
                    if not r.get('agent_connected'):raise RuntimeError('not running')
                    steps.update(connected=True,installed=True,vpn=bool(c.get('vpn',{}).get('enabled')),login=bool(r.get('login_saved')))
                    panel=runtime.panel_url(c)
                    publish('installed','Установленная служба работает. Откройте панель с телефона.',panel)
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
