'use strict';
const token=document.querySelector('meta[name="panel-token"]').content;
const $=s=>document.querySelector(s);
async function api(path,body){let r=await fetch(path,body?{method:'POST',headers:{'Content-Type':'application/json','X-Panel-Token':token},body:JSON.stringify(body)}:{});let d=await r.json();if(!r.ok)throw Error(d.error||'Ошибка');return d}
function render(s){const x=s.steps||{};for(const [key,id] of [['connected','connect'],['installed','install'],['login','login']]){const done=!!x[key];$('#step-'+id).classList.toggle('done',done);$('#badge-'+id).textContent=done?(key==='login'?'✓ Вход сохранён':'✓ Выполнено'):'Ожидает'}const busy=s.phase==='running';$('#install').disabled=busy||!x.connected||!!x.installed;$('#login').disabled=busy||!x.installed;$('#login').textContent=x.login?'Войти заново':'Войти в ChatGPT';const panel=s.panel;$('#panel-card').hidden=!panel;if(panel){$('#panel-url').href=panel.url;$('#panel-url').textContent=panel.url;$('#panel-code').textContent=panel.pairing_code}}
async function run(path,body){try{let d=await api(path,body);render(d);while(d.phase==='running'){ $('#phase').textContent='Выполняется';$('#message').textContent=d.message;await new Promise(r=>setTimeout(r,1000));d=await api('/api/state');render(d)}render(d);$('#phase').textContent=d.phase==='failed'?'Не удалось выполнить шаг':'Готово';$('#message').textContent=d.message;if(d.phase==='fingerprint'){$('#fingerprintValue').textContent='Отпечаток: '+d.message+' — сверьте его с отпечатком колонки.';$('#connect').disabled=false}return d}catch(e){$('#phase').textContent='Ошибка';$('#message').textContent=e.message}}
function endpoint(){return {host:$('#host').value.trim(),port:Number($('#port').value)}}
$('#fingerprint').onclick=()=>run('/api/fingerprint',endpoint());
$('#connect').onclick=()=>{const p=$('#password').value,k=$('#privateKey').value;$('#password').value='';$('#privateKey').value='';run('/api/connect',{...endpoint(),password:p,private_key:k,existing:$('#existingKey').checked,blank:$('#blankPassword').checked})};
$('#install').onclick=()=>run('/api/install',{});
$('#login').onclick=()=>run('/api/login',{});
$('#current').onclick=()=>run('/api/current',{});

api('/api/state').then(render).catch(()=>{});
