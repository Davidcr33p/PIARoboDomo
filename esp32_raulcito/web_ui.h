// ===== Página web embebida (HTML + CSS + JS, sin CDNs) =====
// Una sola página: en modo AP muestra la configuración inicial;
// conectado a la red pide el PIN y muestra el panel de control.
#pragma once
#include <Arduino.h>

const char WEB_UI[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#7a1e1e">
<title>Raulcito bracito</title>
<style>
:root{--g:#7a1e1e;--g2:#5e1717;--gs:#f7eeee;--tx:#231f1f;--mu:#6e6464;--ln:#eadede;--ok:#1d7a3e;--okb:#e2f3e8;--er:#b3261e;--erb:#fbe9e7}
*{box-sizing:border-box}
body{margin:0;font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;background:#faf7f7;color:var(--tx);-webkit-text-size-adjust:100%}
header{position:sticky;top:0;z-index:5;background:var(--g);color:#fff;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;gap:12px}
header h1{margin:0;font-size:18px;line-height:1.2}
header small{opacity:.85;font-size:12px}
.pill{padding:3px 10px;border-radius:99px;font-size:12px;font-weight:600;background:rgba(255,255,255,.18);white-space:nowrap}
main{max-width:640px;margin:0 auto;padding:2px 16px 48px}
.card{background:#fff;border:1px solid var(--ln);border-radius:14px;padding:16px;margin:14px 0}
h2,summary{margin:0 0 12px;font-size:16px;font-weight:700;color:var(--g)}
summary{margin:0;cursor:pointer;list-style:none;display:flex;justify-content:space-between}
summary::-webkit-details-marker{display:none}
summary::after{content:"+";font-size:20px;line-height:1}
details[open] summary{margin-bottom:12px}
details[open] summary::after{content:"\2013"}
p{margin:0 0 10px}
.k3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.kpi{background:var(--gs);border-radius:10px;padding:10px 4px;text-align:center}
.kpi b{display:block;font-size:22px;font-variant-numeric:tabular-nums}
.kpi span{font-size:12px;color:var(--mu)}
.fila{display:flex;justify-content:space-between;gap:12px;padding:7px 0;border-bottom:1px solid #f3eaea}
.fila:last-child{border-bottom:0}
.fila>span:first-child{color:var(--mu)}
.fila>span:last-child{text-align:right;word-break:break-word}
.tag{display:inline-block;padding:2px 9px;border-radius:99px;font-size:12px;font-weight:600}
.si{background:var(--okb);color:var(--ok)}.no{background:var(--erb);color:var(--er)}
.btns{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:12px}
.full{grid-column:1/-1;width:100%}
button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:12px;min-height:44px;background:var(--g);color:#fff;cursor:pointer}
button:active{background:var(--g2)}
button.alt{background:#fff;color:var(--g);border:1.5px solid var(--g)}
button.peligro{background:#fff;color:var(--er);border:1.5px solid var(--er)}
button:disabled{opacity:.55}
label{display:block;font-size:13px;color:var(--mu);margin:12px 0 4px}
input{width:100%;font:inherit;padding:10px 12px;border:1px solid #d9cccc;border-radius:9px;background:#fff;color:var(--tx)}
input:focus{outline:2px solid var(--g);border-color:var(--g)}
input:disabled{background:#f3f0f0}
.chk{display:flex;align-items:center;gap:8px;color:var(--tx);font-size:14px}
.chk input{width:auto;margin:0}
.inl{display:flex;gap:8px}.inl input{flex:1;min-width:0}.inl button{flex:none;padding:10px 14px}
h3{font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:var(--g);margin:20px 0 0;padding-top:14px;border-top:1px solid var(--ln)}
form h3:first-child{margin-top:0;padding-top:0;border:0}
.redes{border:1px solid var(--ln);border-radius:10px;margin-top:8px;max-height:260px;overflow:auto}
.red{display:flex;justify-content:space-between;align-items:center;gap:8px;width:100%;background:#fff;color:var(--tx);border:0;border-bottom:1px solid #f3eaea;border-radius:0;padding:10px 12px;font-weight:400;text-align:left}
.red:last-child{border-bottom:0}
.sen{font-size:12px;color:var(--mu);white-space:nowrap}
.barras{display:inline-flex;gap:2px;align-items:flex-end;height:14px;margin-left:6px;vertical-align:middle}
.barras i{width:4px;background:#ddd;border-radius:1px}.barras i.a{background:var(--g)}
.msg{padding:10px 12px;color:var(--mu);font-size:14px}
.err{color:var(--er);font-size:14px;margin:10px 0 0}
.err:empty{display:none}
.nota{font-size:13px;color:var(--mu)}
.aviso{background:#fff6e0;border:1px solid #f1d48a;color:#6b4e00;border-radius:12px;padding:10px 14px;margin:14px 0;font-size:14px}
ul.ev{list-style:none;margin:0;padding:0}
ul.ev li{display:flex;gap:10px;padding:7px 0;border-bottom:1px solid #f3eaea;font-size:14px}
ul.ev li:last-child{border-bottom:0}
ul.ev time{color:var(--mu);font-size:12px;min-width:64px;padding-top:1px;font-variant-numeric:tabular-nums}
ul.ev b{color:var(--g);font-weight:600}
#pin{font-size:24px;letter-spacing:.5em;text-align:center;margin-bottom:12px}
.oculto{display:none!important}
#toast{position:fixed;left:50%;bottom:20px;transform:translate(-50%,20px);background:var(--tx);color:#fff;padding:10px 16px;border-radius:10px;font-size:14px;opacity:0;pointer-events:none;transition:.25s;max-width:calc(100% - 32px)}
#toast.ver{opacity:1;transform:translate(-50%,0)}
.velo{position:fixed;inset:0;background:rgba(35,20,20,.55);display:flex;align-items:center;justify-content:center;padding:16px;z-index:9}
.velo .card{max-width:420px;width:100%}
</style>
</head>
<body>
<header>
  <div><h1>Raulcito bracito</h1><small id="hNombre">Cargando…</small></div>
  <span class="pill" id="hModo">…</span>
</header>
<main id="main">

<section class="card oculto" id="vLogin">
  <h2>Panel protegido</h2>
  <p class="nota">Escribe el PIN de 4 dígitos que definiste al configurar a Raulcito.</p>
  <form id="fLogin">
    <input id="pin" type="password" inputmode="numeric" maxlength="4" autocomplete="off" aria-label="PIN">
    <button type="submit" class="full">Entrar</button>
    <p class="err" id="loginErr"></p>
  </form>
</section>

<section class="card oculto" id="vAP">
  <h2>Configuración inicial</h2>
  <p>Elige tu red Wi-Fi, define un PIN y captura las claves de SinricPro. Al guardar, Raulcito se reinicia y se conecta solo.</p>
  <p class="nota">¿Prefieres Bluetooth? Abre la app nRF Connect y busca <b>Raulcito-BLE</b> (instrucciones en el README).</p>
</section>

<div class="oculto" id="vPanel">
  <div class="aviso oculto" id="avisoPin">Estás usando el PIN por defecto (1234). Cámbialo en Configuración.</div>
  <section class="card">
    <h2>Sensores</h2>
    <div class="k3">
      <div class="kpi"><b id="sDist">–</b><span>Distancia (cm)</span></div>
      <div class="kpi"><b id="sPir">–</b><span>Movimiento</span></div>
      <div class="kpi"><b id="sLuz">–</b><span id="sLuzT">Luz</span></div>
    </div>
  </section>
  <section class="card">
    <h2>Robot</h2>
    <div class="fila"><span>Modo limpieza</span><span><span class="tag" id="rLimp">–</span></span></div>
    <div class="fila"><span>Brazo</span><span id="rBrazo">–</span></div>
    <div class="fila"><span>Último mensaje del Arduino</span><span id="rArd">–</span></div>
    <div class="btns">
      <button class="full" id="bLimp" data-cmd="LIMPIEZA_ON">Iniciar limpieza</button>
      <button data-cmd="RECOGER">Recoger ahora</button>
      <button class="alt" data-cmd="REPOSO">Reposo</button>
      <button class="alt full" data-cmd="PING">Ping al Arduino</button>
    </div>
  </section>
  <section class="card">
    <h2>Conexión</h2>
    <div id="conex"></div>
  </section>
  <section class="card">
    <h2>Últimos eventos</h2>
    <ul class="ev" id="evs"><li class="nota">Sin eventos todavía</li></ul>
  </section>
</div>

<details class="card oculto" id="cfg">
  <summary>Configuración</summary>
  <form id="fCfg" novalidate>
    <h3>Wi-Fi</h3>
    <label for="ssid">Red (SSID)</label>
    <div class="inl"><input id="ssid" maxlength="32" autocomplete="off" placeholder="Elige o escribe tu red"><button type="button" class="alt" id="bScan">Buscar</button></div>
    <div class="redes oculto" id="redes"></div>
    <label for="pass">Contraseña Wi-Fi</label>
    <input id="pass" type="password" maxlength="63" autocomplete="new-password">
    <label class="chk"><input type="checkbox" id="abierta">Red abierta (sin contraseña)</label>
    <label for="cpin">PIN del panel (4 dígitos)</label>
    <input id="cpin" type="password" inputmode="numeric" maxlength="4" autocomplete="off">

    <h3>Robot</h3>
    <label for="nombre">Nombre del robot</label>
    <input id="nombre" maxlength="32" placeholder="raulcito-01">
    <label for="url">URL de la API (guardar.php)</label>
    <input id="url" type="url" maxlength="200" placeholder="http://servidor/raulcito/guardar.php">

    <h3>SinricPro (Alexa)</h3>
    <p class="nota" style="margin-top:8px">Si el robot está vinculado a raulcito-bracito.web.app, lo que se guarde allá tiene prioridad.</p>
    <label for="app_key">APP_KEY</label>
    <input id="app_key" maxlength="36" autocomplete="off">
    <label for="app_secret">APP_SECRET</label>
    <input id="app_secret" type="password" maxlength="73" autocomplete="off">
    <label for="id_robot">Device ID · Raulcito (Switch)</label>
    <input id="id_robot" maxlength="24" autocomplete="off">
    <label for="id_pir">Device ID · Movimiento (Motion)</label>
    <input id="id_pir" maxlength="24" autocomplete="off">
    <label for="id_ldr">Device ID · Oscuridad (Contact)</label>
    <input id="id_ldr" maxlength="24" autocomplete="off">
    <label class="chk"><input type="checkbox" id="verPass">Mostrar lo que escribo en campos ocultos</label>
    <p class="err" id="cfgErr"></p>
    <button type="submit" class="full" id="bGuardar" style="margin-top:14px">Guardar y conectar</button>
  </form>
  <h3>Restablecer</h3>
  <p class="nota" style="margin-top:8px">Borra Wi-Fi, PIN y claves. Raulcito vuelve a crear la red Raulcito-Setup.</p>
  <button type="button" class="peligro full" id="bReset">Restablecer de fábrica</button>
</details>

</main>
<div class="velo oculto" id="velo"><div class="card"><h2 id="veloT"></h2><p id="veloM"></p></div></div>
<div id="toast" role="status" aria-live="polite"></div>

<script>
const $=i=>document.getElementById(i);
const espera=ms=>new Promise(r=>setTimeout(r,ms));
function ss(k,v){try{if(v===undefined)return sessionStorage.getItem(k)||'';if(v)sessionStorage.setItem(k,v);else sessionStorage.removeItem(k)}catch(e){return''}}
let PIN=ss('pin'),modo='',ultEv=-1,timer=0,activo=true,cfgCargada=false,guardada=false,tienePass=false;

function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e}
function ver(id,si){$(id).classList.toggle('oculto',!si)}
function toast(t){const e=$('toast');e.textContent=t;e.classList.add('ver');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('ver'),2500)}
function velo(t,m){$('veloT').textContent=t;$('veloM').textContent=m;ver('velo',1)}
function seg(n){return n<60?n+' s':n<3600?Math.floor(n/60)+' min':Math.floor(n/3600)+' h'}
function calidad(r){return r>-55?'excelente':r>-67?'buena':r>-78?'regular':'débil'}
function tag(e,on,a,b){e.textContent=on?a:b;e.className='tag '+(on?'si':'no')}

// Llama a la API con el PIN; 401/429 regresan a la pantalla de PIN
async function api(ruta,datos){
  const o={headers:{'X-PIN':PIN}};
  if(datos!==undefined){o.method='POST';o.headers['Content-Type']='application/json';o.body=JSON.stringify(datos)}
  const r=await fetch(ruta,o);let j={};
  try{j=await r.json()}catch(e){}
  if(r.status==401||r.status==429){login(j.error);throw Error('auth')}
  if(!r.ok)throw Error(j.error||'Error '+r.status);
  return j;
}

function login(m){
  PIN='';ss('pin','');modo='';clearTimeout(timer);
  ['vAP','vPanel','cfg'].forEach(i=>ver(i,0));ver('vLogin',1);
  $('hModo').textContent='Bloqueado';
  $('loginErr').textContent=m&&m!='PIN requerido'?m:'';
  $('pin').value='';$('pin').focus();
}
$('fLogin').onsubmit=e=>{
  e.preventDefault();const p=$('pin').value.trim();
  if(!/^\d{4}$/.test(p)){$('loginErr').textContent='El PIN son 4 dígitos';return}
  PIN=p;ss('pin',p);ciclo();
};

// Acomoda las secciones según el modo (AP = configuración primero)
function organizar(){
  const ap=modo=='ap',c=$('cfg');
  ver('vLogin',0);ver('vAP',ap);ver('vPanel',1);ver('cfg',1);
  if(ap){$('vAP').after(c);c.open=true}else $('main').appendChild(c);
  if(!cfgCargada)cargarConfig();
}

// Refresco del estado cada segundo
async function ciclo(){
  clearTimeout(timer);let s;
  try{s=await api('/api/status')}
  catch(e){if(e.message=='auth')return;$('hModo').textContent='Sin conexión';if(activo)timer=setTimeout(ciclo,2000);return}
  if(!activo)return;
  if(s.modo!=modo){modo=s.modo;organizar()}
  pintar(s);
  if(s.ev!=ultEv){ultEv=s.ev;eventos()}
  timer=setTimeout(ciclo,1000);
}

function pintar(s){
  $('hNombre').textContent=s.nombre;
  $('hModo').textContent=s.modo=='ap'?'Modo configuración':'En línea';
  $('sDist').textContent=s.dist<0?'–':s.dist.toFixed(1);
  $('sPir').textContent=s.pir?'Sí':'No';
  $('sLuz').textContent=s.luz;
  $('sLuzT').textContent='Luz · '+(s.oscuro?'oscuro':'claro');
  tag($('rLimp'),s.limpiando,'Activa','Detenida');
  const b=$('bLimp');b.textContent=s.limpiando?'Detener limpieza':'Iniciar limpieza';b.dataset.cmd=s.limpiando?'LIMPIEZA_OFF':'LIMPIEZA_ON';
  $('rBrazo').textContent=s.brazo?'Recogiendo…':'Libre';
  $('rArd').textContent=s.arduino?s.arduino+' · hace '+seg(s.arduino_seg):'Sin mensajes';
  const w=s.wifi,f=[];
  if(s.modo=='ap')f.push(['Red de configuración',w.ap],['IP',w.ip],['Celulares conectados',w.clientes],['Bluetooth','Raulcito-BLE']);
  else f.push(['Wi-Fi',w.ssid],['Señal',w.rssi+' dBm ('+calidad(w.rssi)+')'],['IP',w.ip],['SinricPro',s.sinric_cfg?(s.sinric?'Conectado':'Desconectado'):'Sin configurar',s.sinric_cfg?s.sinric:undefined],['Hora',s.hora||'Sin sincronizar'],['Página web',s.nube||'—']);
  const cx=$('conex');cx.textContent='';
  f.forEach(([k,v,ok])=>{const d=el('div','fila'),b=el('span');d.appendChild(el('span','',k));
    if(ok===undefined)b.textContent=v;else{const t=el('span');tag(t,ok,v,v);b.appendChild(t)}
    d.appendChild(b);cx.appendChild(d)});
  ver('avisoPin',s.modo=='sta'&&s.pin_def);
}

async function eventos(){
  try{
    const j=await api('/api/events'),ul=$('evs');ul.textContent='';
    if(!j.eventos.length){ul.appendChild(el('li','nota','Sin eventos todavía'));return}
    j.eventos.forEach(e=>{const li=el('li'),t=el('span');
      li.appendChild(el('time','',e.fecha?e.fecha.slice(11):'hace '+seg(j.uptime-e.seg)));
      t.appendChild(el('b','',e.tipo));t.append(' '+e.valor);li.appendChild(t);ul.appendChild(li)});
  }catch(e){}
}

// Botones del robot
document.querySelectorAll('[data-cmd]').forEach(b=>b.onclick=async()=>{
  b.disabled=true;
  try{await api('/api/cmd',{cmd:b.dataset.cmd});toast('Enviado: '+b.textContent)}
  catch(x){if(x.message!='auth')toast(x.message)}
  finally{b.disabled=false}
});

// ---------- Configuración ----------
async function cargarConfig(){
  try{
    const c=await api('/api/config');cfgCargada=true;guardada=c.guardada;tienePass=c.tiene_pass;
    ['ssid','nombre','url','id_robot','id_pir','id_ldr'].forEach(k=>$(k).value=c[k]||'');
    $('pass').placeholder=c.tiene_pass?'Guardada (vacío = conservar)':'';
    $('app_key').placeholder=c.tiene_key?'Guardada (vacío = conservar)':'xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx';
    $('app_secret').placeholder=c.tiene_secret?'Guardado (vacío = conservar)':'73 caracteres';
    $('cpin').placeholder=c.guardada?(c.pin_def?'1234 (por defecto)':'Guardado (vacío = conservar)'):'Ej. 4821';
  }catch(e){}
}

function barras(r){const n=r>-55?4:r>-67?3:r>-78?2:1,w=el('span','barras');
  for(let i=1;i<=4;i++){const b=el('i',i<=n?'a':'');b.style.height=(i*3+2)+'px';w.appendChild(b)}return w}

$('bScan').onclick=async()=>{
  const b=$('bScan'),box=$('redes');b.disabled=true;ver('redes',1);
  box.textContent='';box.appendChild(el('div','msg','Buscando redes…'));
  try{
    let j=await api('/api/scan?nuevo=1');
    for(let i=0;j.buscando&&i<25;i++){await espera(700);j=await api('/api/scan')}
    box.textContent='';
    if(!j.redes||!j.redes.length){box.appendChild(el('div','msg','No se encontraron redes. Escribe el nombre a mano.'));return}
    j.redes.forEach(r=>{const x=el('button','red'),s=el('span','sen',r.rssi+' dBm');x.type='button';
      x.appendChild(el('span','',(r.segura?'🔒 ':'')+r.ssid));s.appendChild(barras(r.rssi));x.appendChild(s);
      x.onclick=()=>{$('ssid').value=r.ssid;$('abierta').checked=!r.segura;$('abierta').onchange();ver('redes',0);if(r.segura)$('pass').focus()};
      box.appendChild(x)});
  }catch(x){if(x.message!='auth'){box.textContent='';box.appendChild(el('div','msg','No se pudo buscar: '+x.message))}}
  finally{b.disabled=false}
};

$('abierta').onchange=()=>{$('pass').disabled=$('abierta').checked};
$('verPass').onchange=e=>['pass','app_secret','cpin'].forEach(k=>$(k).type=e.target.checked?'text':'password');

function validar(d){
  if(!d.ssid||d.ssid.length>32)return'Escribe o elige la red Wi-Fi (máx. 32 caracteres).';
  if(!d.abierta&&d.pass&&(d.pass.length<8||d.pass.length>63))return'La contraseña Wi-Fi debe tener de 8 a 63 caracteres.';
  if(!d.abierta&&!d.pass&&!tienePass)return'Escribe la contraseña Wi-Fi o marca «Red abierta».';
  if(d.pin&&!/^\d{4}$/.test(d.pin))return'El PIN deben ser exactamente 4 dígitos.';
  if(!d.pin&&!guardada)return'Define un PIN de 4 dígitos para proteger el panel.';
  if(d.url&&!/^https?:\/\//i.test(d.url))return'La URL debe empezar con http:// o https://';
  if(d.app_key&&!/^[^-]{8}-[^-]{4}-[^-]{4}-[^-]{4}-[^-]{12}$/.test(d.app_key))return'APP_KEY inválida: son 36 caracteres (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).';
  if(d.app_secret&&d.app_secret.length!=73)return'APP_SECRET inválido: son 73 caracteres.';
  for(const[k,n]of[['id_robot','Raulcito'],['id_pir','Movimiento'],['id_ldr','Oscuridad']])
    if(d[k]&&!/^[0-9a-f]{24}$/i.test(d[k]))return'El Device ID de '+n+' debe tener 24 caracteres hexadecimales.';
  return'';
}

$('fCfg').onsubmit=async e=>{
  e.preventDefault();
  const v=k=>$(k).value.trim();
  const d={ssid:v('ssid'),pass:$('pass').value,abierta:$('abierta').checked,pin:v('cpin'),nombre:v('nombre'),url:v('url'),
    app_key:v('app_key'),app_secret:v('app_secret'),id_robot:v('id_robot'),id_pir:v('id_pir'),id_ldr:v('id_ldr')};
  const err=validar(d);$('cfgErr').textContent=err;if(err)return;
  const b=$('bGuardar');b.disabled=true;
  try{
    await api('/api/config',d);activo=false;clearTimeout(timer);if(d.pin)ss('pin',d.pin);
    if(modo=='ap')velo('¡Guardado!','Raulcito se reinicia y se conecta a «'+d.ssid+'». Conecta tu celular a esa misma red y abre http://raulcito.local (o la IP que aparece en el monitor serie).');
    else{velo('Reiniciando…','Raulcito se reinicia con la nueva configuración. Esta página se reconecta sola.');regreso()}
  }catch(x){if(x.message!='auth')$('cfgErr').textContent=x.message}
  finally{b.disabled=false}
};

// Espera a que el ESP32 vuelva después de reiniciar
async function regreso(){
  await espera(6000);
  for(let i=0;i<30;i++){
    try{PIN=ss('pin');const r=await fetch('/api/status',{headers:{'X-PIN':PIN}});
      if(r.ok||r.status==401){ver('velo',0);activo=true;cfgCargada=false;modo='';$('pass').value='';$('cpin').value='';$('app_secret').value='';ciclo();toast('Raulcito está de vuelta');return}}catch(e){}
    await espera(2000);
  }
  $('veloM').textContent='No responde en esta dirección. Revisa la IP en el monitor serie.';
}

$('bReset').onclick=async()=>{
  if(!confirm('¿Borrar toda la configuración?\nRaulcito volverá a crear la red Raulcito-Setup.'))return;
  try{await api('/api/reset',{});activo=false;clearTimeout(timer);ss('pin','');
    velo('Restablecido','Se borró la configuración. Conéctate a la red Raulcito-Setup para configurarlo de nuevo.')}
  catch(x){if(x.message!='auth')toast(x.message)}
};

ciclo();
</script>
</body>
</html>
)HTML";
