'use strict';
const token=document.querySelector('meta[name="panel-token"]').content;
const $=s=>document.querySelector(s);
async function api(path,body){let r=await fetch(path,body?{method:'POST',headers:{'Content-Type':'application/json','X-Panel-Token':token},body:JSON.stringify(body)}:{});let d=await r.json();if(!r.ok)throw Error(d.error||'Ошибка');return d}
async function run(path,body){try{let d=await api(path,body);while(d.phase==='running'){ $('#phase').textContent='Выполняется';$('#message').textContent=d.message;await new Promise(r=>setTimeout(r,1000));d=await api('/api/state')}$('#phase').textContent=d.phase==='failed'?'Не удалось выполнить шаг':'Готово';$('#message').textContent=d.message;if(d.phase==='fingerprint'){$('#fingerprintValue').textContent='Отпечаток: '+d.message+' — сверьте его с отпечатком колонки.';$('#connect').disabled=false}return d}catch(e){$('#phase').textContent='Ошибка';$('#message').textContent=e.message}}
function endpoint(){return {host:$('#host').value.trim(),port:Number($('#port').value)}}
$('#fingerprint').onclick=()=>run('/api/fingerprint',endpoint());
$('#connect').onclick=()=>{const p=$('#password').value;$('#password').value='';run('/api/connect',{...endpoint(),password:p})};
$('#install').onclick=()=>run('/api/install',{});
$('#login').onclick=()=>run('/api/login',{});
$('#current').onclick=()=>run('/api/current',{});
