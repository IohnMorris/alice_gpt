#pragma once
#include "common.hpp"
#include <openssl/crypto.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <map>
#include <iomanip>

inline const char *control_page=R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Чаты на Станции</title>
<style>:root{color-scheme:dark}*{box-sizing:border-box}body{font:16px/1.5 system-ui,-apple-system,"Segoe UI",sans-serif;background:radial-gradient(90% 55% at 50% -10%,#233d70 0%,#111a2a 48%,#080d16 100%);color:#f3f6ff;max-width:720px;min-height:100vh;margin:0 auto;padding:34px 18px}h1{font-size:clamp(30px,8vw,42px);letter-spacing:-.05em;margin:0}h2{font-size:18px;margin:0 0 12px}.eyebrow,small{color:#9eadc8}section{background:linear-gradient(145deg,#1e2b42e8,#121b2ce8);border:1px solid #334968;border-radius:22px;padding:20px;margin:16px 0;box-shadow:0 18px 45px #0005}button,input,select{font:inherit;border-radius:12px;padding:12px 14px;border:1px solid #415776;background:#101a2a;color:inherit;box-sizing:border-box}button{cursor:pointer;background:linear-gradient(135deg,#5d7cff,#3f57dc);border:0;font-weight:750;transition:transform .15s,filter .15s}button:active{transform:scale(.97)}button.secondary{background:#293952}button.danger{background:#a63f53}label{display:block;margin-top:14px;color:#bdc9df;font-size:13px;font-weight:700;letter-spacing:.02em}select,input{width:100%;margin-top:6px}.row{display:flex;gap:9px;flex-wrap:wrap}.row button{flex:1 1 140px}small{display:block;margin-top:10px}pre{white-space:pre-wrap;word-break:break-word;font-size:12px}.error{color:#ffb8c4;min-height:1.5em}#phase{margin:8px 0 22px;color:#aebde0;font-weight:700}#account{padding:11px 13px;border-radius:12px;background:#0c1422;color:#a9d9c3}@media(max-width:480px){body{padding:24px 14px}section{padding:17px}.row button{flex-basis:100%}}</style>
<p class="eyebrow">ALISA VOICE · MINI 2</p><h1>Управление ChatGPT</h1><p id="phase">Подключение…</p>
<section id="pair" hidden><p>Введите код сопряжения этой Станции.</p><input id="pin" inputmode="numeric" autocomplete="one-time-code" maxlength="8"><button onclick="pair()">Подключиться</button><p id="pairError" class="error"></p></section>
<main id="panel" hidden><section><div class="row"><button onclick="command('open')">Открыть чат</button><button onclick="command('new')">Новый чат</button><button class="secondary" onclick="command('close')">Вернуться к Алисе</button></div>
<label for="chat">Разговор</label><select id="chat"></select><div class="row"><button onclick="command('select',{chat_id:byId('chat').value})">Открыть выбранный</button><button class="secondary" onclick="command('previous')">Предыдущий чат</button><button class="danger" onclick="removeChat()">Удалить выбранный</button></div>
<label for="title">Название выбранного разговора</label><input id="title" maxlength="60"><button class="secondary" onclick="command('rename',{chat_id:byId('chat').value,title:byId('title').value})">Сохранить название</button></section>
<section><label for="voice">Голос</label><select id="voice"><option value="fathom">Arbor</option><option value="breeze">Breeze</option><option value="cove">Cove</option><option value="ember">Ember</option><option value="juniper">Juniper</option><option value="maple">Maple</option><option value="glimmer">Sol</option><option value="orbit">Spruce</option><option value="vale">Vale</option></select><button onclick="command('voice',{voice:byId('voice').value})">Сохранить голос</button><p><small>Новый голос применяется при следующем открытии разговора. Выбор сохраняется после выключения.</small></p><p id="account"></p></section>
<section><label for="led">Подсветка GPT</label><select id="led"><option value="rainbow">Радуга</option><option value="red">Красный</option><option value="blue">Синий</option><option value="green">Зелёный</option><option value="purple">Фиолетовый</option><option value="white">Белый</option><option value="pulse-red">Пульсация: красный</option><option value="pulse-blue">Пульсация: синий</option><option value="pulse-green">Пульсация: зелёный</option><option value="pulse-purple">Пульсация: фиолетовый</option><option value="pulse-white">Пульсация: белый</option><option value="off">Выключить</option></select><button class="secondary" onclick="command('led',{style:byId('led').value})">Применить подсветку</button><p><small>Меняется без перезапуска чата. При физическом mute штатная индикация сохраняет приоритет.</small></p></section>
<section><h2>Голосовые фразы</h2><label>Открыть чат<input id="phrase-open" maxlength="200"></label><label>Закрыть чат<input id="phrase-close" maxlength="200"></label><label>Новый чат<input id="phrase-new" maxlength="200"></label><label>Предыдущий чат<input id="phrase-previous" maxlength="200"></label><button onclick="savePhrases()">Сохранить фразы</button><p><small>Фразы применяются сразу, без перезапуска колонки.</small></p></section>
<p id="error" class="error"></p><small id="memory"></small><details><summary>Диагностика</summary><pre id="debug"></pre></details></main>
<script>const byId=id=>document.getElementById(id);let lastChats='',lastVoice='',lastSelected='';
async function pair(){let r=await fetch('/pair',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({code:byId('pin').value})});if(!r.ok)byId('pairError').textContent='Код не подошёл. Проверьте код и повторите позже.';else{byId('pin').value='';await update()}}
async function removeChat(){let id=byId('chat').value;if(!id||!confirm('Удалить выбранный чат?'))return;await command('delete',{chat_id:id})}
async function savePhrases(){let phrases={open:byId('phrase-open').value.trim(),close:byId('phrase-close').value.trim(),new:byId('phrase-new').value.trim(),previous:byId('phrase-previous').value.trim()};await command('phrases',{phrases})}
async function command(action,extra={}){let box=byId('error');box.textContent='Применяем…';try{let r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,...extra})});let result=await r.json();if(!r.ok||!result.ok)throw Error('Команда не выполнена');box.textContent='✓ Настройка применена';await update()}catch(e){box.textContent='Не удалось применить. Проверьте состояние службы.'}}
async function update(){try{let r=await fetch('/api/status');byId('pair').hidden=r.status!==401;byId('panel').hidden=!r.ok;if(!r.ok){byId('phase').textContent='Нужно сопряжение';return}let s=await r.json();let names={idle:'Обычная Алиса',connecting:'Подключаем ChatGPT…',active:'Разговор с ChatGPT',stopping:'Возвращаем Алису…'};byId('phase').textContent=names[s.phase]||s.phase;byId('account').textContent=s.login_saved?'Вход в ChatGPT сохранён на Станции.':'Нужен первоначальный вход в ChatGPT.';
let chats=JSON.stringify(s.settings.chats.map(c=>[c.id,c.title,c.confirmed]));if(chats!==lastChats){let choice=byId('chat').value;lastChats=chats;byId('chat').replaceChildren(...s.settings.chats.map(c=>{let o=document.createElement('option');o.value=c.id;o.textContent=c.title+(c.confirmed?'':' · ещё не подключён');return o}));byId('chat').value=s.settings.chats.some(c=>c.id===choice)?choice:s.settings.selected}if(s.settings.selected!==lastSelected){lastSelected=s.settings.selected;byId('chat').value=lastSelected}
if(s.settings.voice!==lastVoice){lastVoice=s.settings.voice;byId('voice').value=lastVoice}if(s.led_style)byId('led').value=s.led_style;if(s.phrases)for(let key of ['open','close','new','previous']){let field=byId('phrase-'+key);if(document.activeElement!==field)field.value=s.phrases[key]||''}if(document.activeElement!==byId('title')){let c=s.settings.chats.find(c=>c.id===byId('chat').value);byId('title').value=c?c.title:''}
let errors={voice_daily_limit_reached:'Дневной лимит голосового ChatGPT исчерпан. Попробуйте после его сброса.',voice_session_ended:'ChatGPT завершил голосовой сеанс. Можно открыть его заново.',login_required:'Нужно снова войти в ChatGPT.',no_previous_chat:'Предыдущего чата пока нет.',native_focus_not_ready:'Не удалось получить аудиофокус колонки.',chat_account_mismatch:'Этот чат принадлежит другому аккаунту.'};byId('error').textContent=s.error?(errors[s.error]||(s.error.startsWith('voice_signaling_')?'ChatGPT не принял подключение. Попробуйте позже.':s.error.startsWith('https_transport_')?'Не удалось связаться с ChatGPT.':'Не удалось открыть разговор. Подробности в диагностике.')):'';byId('memory').textContent='Память клиента: '+(s.rss_kib/1024).toFixed(1)+' МБ';byId('debug').textContent=JSON.stringify(s,null,2)}catch(e){byId('phase').textContent='Нет связи со Станцией'}}update();setInterval(update,3000);</script></html>)HTML";

inline std::string panel_page(const fs::path &base){
    auto path=base/"panel.html";std::error_code ec;auto size=fs::file_size(path,ec);
    if(!ec&&size>0&&size<=256*1024){std::ifstream page(path);std::string text((std::istreambuf_iterator<char>(page)),{});if(!text.empty())return text;}
    return control_page;
}

inline constexpr unsigned short control_port=2000;

inline json control_auth(const fs::path &state,bool reset=false){
    auto file=state/"ui-auth.json";
    if(!reset&&fs::exists(file)){
        auto auth=read_json(file);
        if(auth.value("code",std::string()).size()==8&&auth.value("key",std::string()).size()>=32)return auth;
    }
    unsigned char b[4];if(RAND_bytes(b,4)!=1)throw std::runtime_error("random_failed");uint32_t n=0;std::memcpy(&n,b,4);
    std::ostringstream code;code<<std::setw(8)<<std::setfill('0')<<(n%100000000);
    json auth={{"key",random_id()+random_id()},{"code",code.str()}};write_json(file,auth);return auth;
}

class ControlServer {
    int listener=-1;unsigned short port;fs::path state;json auth;
    std::function<json()> status;
    std::function<json(const json &)> command;
    unsigned int failed_pairs=0;Clock::time_point blocked_until{};
    static std::string lower(std::string text){for(auto &c:text)if(c>='A'&&c<='Z')c+=32;return text;}
    bool secret_equal(const std::string &a,const std::string &b){return a.size()==b.size()&&CRYPTO_memcmp(a.data(),b.data(),a.size())==0;}
    void reply(int fd,int code,const std::string &body,const std::string &type="application/json",const std::string &extra={}){
        std::string header="HTTP/1.1 "+std::to_string(code)+" Response\r\nContent-Type: "+type+
            "\r\nContent-Length: "+std::to_string(body.size())+"\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n"+extra+"\r\n";
        std::string text=header+body;size_t offset=0;
        while(offset<text.size()){ssize_t n=send(fd,text.data()+offset,text.size()-offset,MSG_NOSIGNAL);if(n<=0)break;offset+=n;}
    }
    void request(int fd){
        auth=control_auth(state);
        timeval timeout{2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        std::string raw;char bytes[2048];size_t end;
        while((end=raw.find("\r\n\r\n"))==std::string::npos){
            auto n=recv(fd,bytes,sizeof(bytes),0);if(n<=0||raw.size()+n>8192){reply(fd,400,"{}");return;}raw.append(bytes,n);
        }
        std::istringstream stream(raw.substr(0,end));std::string line,method,path,version;
        if(!std::getline(stream,line)){reply(fd,400,"{}");return;}
        {std::istringstream first(line);first>>method>>path>>version;}
        if(path.size()>128||version!="HTTP/1.1"||(method!="GET"&&method!="POST")){reply(fd,400,"{}");return;}
        std::map<std::string,std::string> headers;
        while(std::getline(stream,line)){
            if(!line.empty()&&line.back()=='\r')line.pop_back();auto colon=line.find(':');if(colon==std::string::npos){reply(fd,400,"{}");return;}
            std::string name=lower(line.substr(0,colon)),value=line.substr(colon+1);
            while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(value.begin());
            if(headers.count(name)){reply(fd,400,"{}");return;}headers[name]=value;
        }
        sockaddr_in local{};socklen_t length=sizeof(local);getsockname(fd,reinterpret_cast<sockaddr *>(&local),&length);
        char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&local.sin_addr,ip,sizeof(ip));
        std::string host=headers["host"],suffix=":"+std::to_string(port);
        if(host!=std::string(ip)+suffix&&host!="localhost"+suffix&&host!="127.0.0.1"+suffix){reply(fd,403,"{}");return;}
        if(headers.count("origin")&&headers["origin"]!="http://"+host){reply(fd,403,"{}");return;}
        if(headers.count("transfer-encoding")){reply(fd,400,"{}");return;}
        size_t wanted=0;
        if(headers.count("content-length")){
            const auto &text=headers["content-length"];
            if(text.empty()||text.find_first_not_of("0123456789")!=std::string::npos||text.size()>5){reply(fd,400,"{}");return;}
            wanted=static_cast<size_t>(std::stoul(text));if(wanted>16384){reply(fd,413,"{}");return;}
        }
        std::string body=raw.substr(end+4);
        while(body.size()<wanted){auto n=recv(fd,bytes,sizeof(bytes),0);if(n<=0){reply(fd,400,"{}");return;}body.append(bytes,n);}
        body.resize(wanted);
        if(method=="GET"&&path=="/"){reply(fd,200,panel_page(state.parent_path()),"text/html; charset=utf-8");return;}
        if(method=="POST"&&path=="/pair"){
            if(Clock::now()<blocked_until){reply(fd,429,"{}");return;}
            auto data=json::parse(body);std::string code=data.value("code",std::string());
            if(!secret_equal(code,auth["code"].get<std::string>())){
                if(++failed_pairs>=5){blocked_until=Clock::now()+std::chrono::seconds(60);failed_pairs=0;}
                reply(fd,401,"{}");return;
            }
            failed_pairs=0;
            reply(fd,200,"{}","application/json","Set-Cookie: standalone_auth="+auth["key"].get<std::string>()+"; HttpOnly; SameSite=Strict; Path=/; Max-Age=2592000\r\n");return;
        }
        std::string key=headers["x-standalone-key"],cookie=headers["cookie"];
        auto offset=cookie.find("standalone_auth=");
        if(key.empty()&&offset!=std::string::npos){offset+=16;auto next=cookie.find(';',offset);key=cookie.substr(offset,next==std::string::npos?next:next-offset);}
        if(!secret_equal(key,auth["key"].get<std::string>())){reply(fd,401,"{}");return;}
        if(method=="GET"&&path=="/api/status"){reply(fd,200,status().dump());return;}
        if(method=="POST"&&path=="/api/command"){reply(fd,200,command(json::parse(body)).dump());return;}
        reply(fd,404,"{}");
    }
public:
    ControlServer(unsigned short number,const fs::path &state,std::function<json()> get,std::function<json(const json &)> act)
        :port(number),state(state),status(std::move(get)),command(std::move(act)){
        auth=control_auth(state);
        listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0)throw std::runtime_error("control_socket_failed");
        int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
        sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);addr.sin_addr.s_addr=INADDR_ANY;
        if(bind(listener,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))<0||listen(listener,4)<0)throw std::runtime_error("control_bind_failed");
    }
    void run(){
        while(!stopping){pollfd p{listener,POLLIN,0};if(poll(&p,1,500)<=0)continue;
            int client=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);if(client<0)continue;
            try{request(client);}catch(const std::exception &){reply(client,400,"{}");}close(client);
        }
    }
    ~ControlServer(){if(listener>=0)close(listener);}
};

inline json control_request(const fs::path &state,const json &body){
    auto key=read_json(state/"ui-auth.json")["key"].get<std::string>();
    CURL *curl=curl_easy_init();if(!curl)throw std::runtime_error("control_client_failed");
    std::string text=body.dump(),response;curl_slist *headers=nullptr;
    headers=curl_slist_append(headers,("X-Standalone-Key: "+key).c_str());headers=curl_slist_append(headers,"Content-Type: application/json");
    std::string url="http://127.0.0.1:"+std::to_string(control_port)+(body.empty()?"/api/status":"/api/command");
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_PROXY,"");
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(curl,CURLOPT_TIMEOUT,5L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,http_body);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response);
    if(!body.empty()){curl_easy_setopt(curl,CURLOPT_POSTFIELDS,text.c_str());curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,text.size());}
    auto result=curl_easy_perform(curl);long status=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    curl_slist_free_all(headers);curl_easy_cleanup(curl);
    if(result!=CURLE_OK||status!=200)throw std::runtime_error("control_unavailable");return json::parse(response);
}
