#pragma once
// Settings page served by the board. Self-contained: the setup hotspot has no internet.

static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Stock Ticker Setup</title>
<style>
:root{--fg:#1a1a1a;--muted:#666;--line:#d6d6d6;--bg:#f4f4f2;--card:#fff;--accent:#1f5fbf;--bad:#b3261e;--good:#1e7a34}
*{box-sizing:border-box}
body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color:var(--fg);background:var(--bg)}
header{background:#111;color:#fff;padding:14px 16px}
header h1{margin:0;font-size:18px}
header p{margin:2px 0 0;color:#bbb;font-size:13px}
main{max-width:960px;margin:0 auto;padding:12px 16px 90px}
section{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px;margin:12px 0}
h2{font-size:16px;margin:0 0 4px}
.hint{color:var(--muted);font-size:13px;margin:0 0 10px}
label{display:block;font-size:13px;color:var(--muted);margin-bottom:3px}
input,select,button{font:inherit}
input,select{width:100%;padding:8px;border:1px solid var(--line);border-radius:6px;background:#fff;color:var(--fg)}
.row{display:grid;gap:8px;grid-template-columns:1fr 1fr;margin-bottom:8px}
.stock{border-top:1px solid var(--line);padding-top:12px;margin-top:12px}
.stock:first-of-type{border-top:0;padding-top:0;margin-top:0}
.sym{display:flex;gap:8px;align-items:end}
.sym>div{flex:1}
.pos{display:grid;gap:6px;grid-template-columns:.8fr 1.15fr .8fr .9fr .9fr .9fr 1.25fr;margin-top:6px;align-items:center}
.pos.head{font-size:12px;color:var(--muted)}
.amt{font-variant-numeric:tabular-nums;text-align:right;font-weight:600;line-height:1.2}
.held{display:block;font-size:12px;color:var(--fg);font-weight:600;margin-top:2px;min-height:1em}
.held.future{color:var(--bad)}
.amt small{display:block;font-weight:400;color:var(--muted);font-size:12px}
.sumrow{display:flex;justify-content:flex-end;gap:16px;font-size:13px;color:var(--muted);margin-top:6px}
.check{font-size:13px;margin-top:4px;min-height:1.2em}
.ok{color:var(--good)} .err{color:var(--bad)}
textarea{width:100%;min-height:220px;padding:8px;border:1px solid var(--line);border-radius:6px;font:13px/1.4 ui-monospace,Menlo,Consolas,monospace}
.sounds{display:flex;flex-wrap:wrap;gap:6px;margin:6px 0}
h3{font-size:14px;margin:16px 0 4px}
code{background:#eee;padding:1px 4px;border-radius:3px;font-size:13px}
input.bad{border-color:var(--bad);background:#fff3f2}
button{padding:8px 14px;border-radius:6px;border:1px solid var(--line);background:#fff;cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
.inline{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.inline input[type=checkbox]{width:auto}
.bar{position:fixed;left:0;right:0;bottom:0;background:#fff;border-top:1px solid var(--line);padding:10px 16px}
.bar .inner{max-width:960px;margin:0 auto;display:flex;gap:12px;align-items:center}
#msg{flex:1;font-size:14px}
@media (max-width:640px){.pos{grid-template-columns:1fr 1fr;padding-bottom:8px;border-bottom:1px dashed var(--line)}.pos.head{display:none}.amt{grid-column:span 2}.row{grid-template-columns:1fr}}
</style></head><body>
<header><h1>Stock Ticker Setup</h1><p id="status">Loading…</p></header>
<main>
<section>
  <h2>Wi-Fi networks</h2>
  <p class="hint">Up to 4 networks. The ticker connects to the strongest one it can find.</p>
  <div id="wifi"></div>
  <datalist id="nets"></datalist>
  <button type="button" id="scan">Scan for networks</button>
</section>

<section>
  <h2>Stocks (SET Thailand)</h2>
  <p class="hint">Up to 3 SET symbols, for example SJWD, PTT, CPALL. Type the symbol only, without ".BK".<br>
  <b>Long</b>: entry = price you bought at, target = price you want to sell at.<br>
  <b>Short</b>: entry = price you sold at, target = price you want to buy back at.<br>
  The alarm rings when the price reaches a target. Leave a row empty to skip it.</p>
  <div id="stocks"></div>
</section>

<section>
  <h2>Alarm</h2>
  <div class="inline" style="margin-bottom:10px"><input type="checkbox" id="alarmOn"><label for="alarmOn" style="margin:0;color:var(--fg)">Ring when a target price is reached</label></div>
  <div class="row">
    <div><label for="alarmVolume">Volume (<span id="volv"></span>)</label><input type="range" id="alarmVolume" min="0" max="100"></div>
    <div><label for="alarmSeconds">Ring for (seconds)</label><input type="number" id="alarmSeconds" min="3" max="600"></div>
  </div>
  <button type="button" id="test">Test alarm</button>
  <p class="hint" style="margin-top:8px">Press any button on the ticker to silence the alarm.</p>
</section>

<section>
  <h2>Display</h2>
  <div class="row">
    <div><label for="fetchSeconds">Update prices every (seconds)</label><input type="number" id="fetchSeconds" min="30" max="3600"></div>
    <div><label for="rotateSeconds">Switch stock page every (seconds, 0 = off)</label><input type="number" id="rotateSeconds" min="0" max="3600"></div>
  </div>
</section>

<section>
  <h2>SD card</h2>
  <p class="hint" id="sdStatus">Checking…</p>
  <div id="sdBody" hidden>
    <h3>Settings backup</h3>
    <p class="hint">Saved automatically to <code>/ticker/settings.json</code> every time you save (Wi-Fi passwords included, so keep the card safe).
    To set up a ticker from a file, copy it to the card as <code>/ticker/restore.json</code> and power on.</p>
    <div class="inline"><button type="button" id="sdBackup">Back up now</button><button type="button" id="sdRestore">Restore from card</button></div>

    <h3>Alarm sounds</h3>
    <p class="hint">Copy PCM <code>.wav</code> files (8 or 16-bit, mono or stereo, 8–48 kHz) to <code>/ticker/sounds/</code>. The first match is used:
    <code>SJWD-long.wav</code> / <code>SJWD-short.wav</code>, then <code>SJWD.wav</code>, then <code>long.wav</code> / <code>short.wav</code>, then <code>alarm.wav</code>, otherwise the built-in beep.</p>
    <div class="sounds" id="sounds"></div>
    <div class="inline"><input type="file" id="soundFile" accept=".wav,audio/wav" style="width:auto;flex:1">
      <button type="button" id="soundUp">Upload sound</button></div>

    <h3>Market holidays</h3>
    <p class="hint">One holiday per line as <code>YYYY-MM-DD  Name</code>. The market shows as closed on these days. Stored in <code>/ticker/holidays.txt</code>. <span id="holSrc"></span></p>
    <textarea id="holidays" spellcheck="false"></textarea>
    <div class="inline" style="margin-top:8px"><button type="button" id="holSave">Save holidays</button><span id="holMsg"></span></div>

    <h3>Firmware</h3>
    <p class="hint">Installed: <b id="fwVer"></b>. To update, copy a new <code>firmware.bin</code> to <code>/ticker/firmware.bin</code>; it is installed at the next power-on.</p>
    <div class="inline" style="margin-bottom:8px"><input type="file" id="fwFile" accept=".bin" style="width:auto;flex:1">
      <button type="button" id="fwUp">Upload firmware</button></div>
    <div class="inline" id="fwPending" hidden><span id="fwInfo"></span><button type="button" id="fwInstall">Install now</button></div>
  </div>
</section>

<section>
  <h2>Setup hotspot</h2>
  <p class="hint">Password for the ticker's own setup Wi-Fi. Leave blank to keep the current one.</p>
  <input type="password" id="apPass" placeholder="(unchanged)" autocomplete="new-password">
  <p class="hint" style="margin-top:10px" id="apHint">You can also start the hotspot now (or hold MENU on the ticker for 3 seconds).</p>
  <button type="button" id="apNow">Start setup hotspot</button>
</section>
</main>
<div class="bar"><div class="inner"><span id="msg"></span><button class="primary" id="save">Save &amp; restart</button></div></div>

<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let cfg=null;

function wifiRows(){
  let h='';
  for(let i=0;i<4;i++){
    const w=(cfg.wifi||[])[i]||{};
    h+=`<div class="row"><div><label>Network ${i+1}</label><input id="ssid${i}" list="nets" value="${esc(w.ssid)}" autocomplete="off"></div>
    <div><label>Password</label><input id="pass${i}" type="password" placeholder="${w.hasPass?'(unchanged)':''}" autocomplete="new-password" data-has="${w.hasPass?1:0}"></div></div>`;
  }
  $('wifi').innerHTML=h;
}

function posPlaceholders(s,j){
  const short=$(`side${s}_${j}`).value==='short';
  $(`entry${s}_${j}`).placeholder=short?'Sold at':'Bought at';
  $(`target${s}_${j}`).placeholder=short?'Buy back at':'Sell at';
}

function stockRows(){
  let h='';
  for(let s=0;s<3;s++){
    const st=(cfg.stocks||[])[s]||{pos:[]};
    h+=`<div class="stock"><div class="sym"><div><label>Stock ${s+1} symbol</label>
      <input id="sym${s}" value="${esc(st.symbol)}" placeholder="${s?'optional':'e.g. SJWD'}" autocapitalize="characters" autocomplete="off"></div>
      <button type="button" data-check="${s}">Check</button></div>
      <div class="check" id="chk${s}"></div>
      <div class="pos head"><span>Ref #</span><span>Entry date</span><span>Side</span><span>Shares</span><span>Entry price</span><span>Target price</span><span style="text-align:right">Total amount (THB)</span></div>`;
    for(let j=0;j<3;j++){
      const p=(st.pos||[])[j]||{};
      h+=`<div class="pos">
        <input id="ref${s}_${j}" placeholder="Ref #" maxlength="20" value="${esc(p.ref)}" autocomplete="off">
        <div><input id="date${s}_${j}" type="date" value="${esc(p.date)}" title="Entry date"><span class="held" id="held${s}_${j}"></span></div>
        <select id="side${s}_${j}" data-s="${s}" data-j="${j}"><option value="long">Long</option><option value="short"${p.side==='short'?' selected':''}>Short</option></select>
        <input id="vol${s}_${j}" inputmode="numeric" placeholder="Shares" value="${p.volume??''}" autocomplete="off">
        <input id="entry${s}_${j}" inputmode="decimal" value="${p.entry??''}" autocomplete="off">
        <input id="target${s}_${j}" inputmode="decimal" value="${p.target||''}" autocomplete="off">
        <div class="amt" id="amt${s}_${j}"></div></div>`;
    }
    h+=`<div class="sumrow" id="sum${s}"></div></div>`;
  }
  $('stocks').innerHTML=h;
  for(let s=0;s<3;s++){for(let j=0;j<3;j++)posPlaceholders(s,j);updateAmounts(s);}
}

const money=v=>v.toLocaleString('en-US',{minimumFractionDigits:2,maximumFractionDigits:2});
// Shares x price, in whole satang so 9.35 x 50,000 is exactly 467,500.00.
const amount=(shares,price)=>Math.round(price*100)*shares/100;

// Live totals: entry amount per position, value at target, and per-stock sums.
// Days from the entry date to today (local calendar days).
function updateHeld(s,j){
  const v=$(`date${s}_${j}`).value,el=$(`held${s}_${j}`);
  el.className='held';
  if(!v){el.textContent='';return;}
  const [y,m,d]=v.split('-').map(Number),t=new Date();
  const days=Math.round((Date.UTC(t.getFullYear(),t.getMonth(),t.getDate())-Date.UTC(y,m-1,d))/86400000);
  if(days<0){el.textContent=`in ${-days} day${days===-1?'':'s'} (future date)`;el.className='held future';}
  else el.textContent=days===0?'today':`${days.toLocaleString('en-US')} day${days===1?'':'s'} held`;
}

function updateAmounts(s){
  for(let j=0;j<3;j++)updateHeld(s,j);
  const tot={long:{n:0,shares:0,amt:0},short:{n:0,shares:0,amt:0}};
  for(let j=0;j<3;j++){
    const shares=Math.abs(num(`vol${s}_${j}`)),entry=num(`entry${s}_${j}`),target=num(`target${s}_${j}`);
    const short=$(`side${s}_${j}`).value==='short',el=$(`amt${s}_${j}`);
    if(!shares||!entry||isNaN(shares)||isNaN(entry)){el.innerHTML='';continue;}
    const amt=amount(shares,entry);
    let h=money(amt)+`<small>${short?'received':'paid'}</small>`;
    if(target&&!isNaN(target)){
      const pl=short?amt-amount(shares,target):amount(shares,target)-amt;
      h+=`<small>at target ${money(amount(shares,target))} (${pl>=0?'+':''}${money(pl)})</small>`;
    }
    el.innerHTML=h;const t=tot[short?'short':'long'];t.n++;t.shares+=shares;t.amt+=amt;
  }
  const part=(k,verb)=>tot[k].n?`${k==='long'?'Long':'Short'}: ${tot[k].shares.toLocaleString('en-US')} shares · ${money(tot[k].amt)} THB ${verb}`:'';
  $('sum'+s).textContent=tot.long.n+tot.short.n>1?[part('long','paid'),part('short','received')].filter(Boolean).join('   |   '):'';
}

function fill(){
  wifiRows();stockRows();
  $('alarmOn').checked=cfg.alarmOn;
  $('alarmVolume').value=cfg.alarmVolume;$('volv').textContent=cfg.alarmVolume;
  $('alarmSeconds').value=cfg.alarmSeconds;
  $('fetchSeconds').value=cfg.fetchSeconds;
  $('rotateSeconds').value=cfg.rotateSeconds;
}

// Lenient number parsing: "1,000", " 9.50 " and "-500" are fine. null = blank, NaN = not a number.
function num(id){
  const v=$(id).value.replace(/[,\s]/g,'');
  if(v==='')return null;
  return /^[-+]?(\d+\.?\d*|\.\d+)$/.test(v)?parseFloat(v):NaN;
}

// Returns {out, errors}. Each error names the stock, position and field.
function gather(){
  const errors=[];
  document.querySelectorAll('input.bad').forEach(e=>e.classList.remove('bad'));
  const bad=(id,text)=>{$(id).classList.add('bad');errors.push(text);};
  const out={wifi:[],stocks:[],alarmOn:$('alarmOn').checked,alarmVolume:+$('alarmVolume').value,
    alarmSeconds:+$('alarmSeconds').value,fetchSeconds:+$('fetchSeconds').value,
    rotateSeconds:+$('rotateSeconds').value,apPass:$('apPass').value};
  for(let i=0;i<4;i++){
    const ssid=$('ssid'+i).value.trim(),pass=$('pass'+i).value;
    if(ssid)out.wifi.push({ssid,pass,keep:!pass&&$('pass'+i).dataset.has==='1'});
  }
  for(let s=0;s<3;s++){
    const symbol=$('sym'+s).value.trim().toUpperCase().replace(/\.BK$/,'');
    if(!symbol)continue;
    const pos=[];
    for(let j=0;j<3;j++){
      const v=`vol${s}_${j}`,e=`entry${s}_${j}`,t=`target${s}_${j}`;
      let volume=num(v),entry=num(e),target=num(t);
      const ref=$(`ref${s}_${j}`).value.trim(),date=$(`date${s}_${j}`).value;
      if(volume===null&&entry===null&&target===null&&!ref&&!date)continue;  // empty row
      const side=$(`side${s}_${j}`).value,isShort=side==='short';
      const where=`${symbol} position ${j+1} (${isShort?'Short':'Long'})`;
      if(volume===null)bad(v,`${where}: enter the number of shares`);
      else if(isNaN(volume)||Math.abs(volume)<1||!Number.isInteger(Math.abs(volume)))bad(v,`${where}: shares must be a whole number, e.g. 1000`);
      if(entry===null)bad(e,`${where}: enter the price you ${isShort?'sold':'bought'} at`);
      else if(isNaN(entry)||entry<=0)bad(e,`${where}: the ${isShort?'sold':'bought'} price must be a number above 0`);
      if(target!==null&&(isNaN(target)||target<0))bad(t,`${where}: the ${isShort?'buy back':'sell'} price must be a number`);
      pos.push({side,volume:Math.abs(volume||0),entry:entry||0,target:target||0,ref,date});
    }
    out.stocks.push({symbol,pos});
  }
  return {out,errors};
}

function msg(t,cls){$('msg').textContent=t;$('msg').className=cls||'';}

async function load(){
  const [c,st]=await Promise.all([fetch('/api/config').then(r=>r.json()),fetch('/api/status').then(r=>r.json())]);
  cfg=c;fill();
  if(st.ap){$('apNow').hidden=true;$('apHint').hidden=true;}
  $('status').textContent=st.ap?`Setup hotspot "${st.apSsid}" · ${st.sta?'online via '+st.staSsid:'not connected to the internet'}`
                                :`Connected to ${st.staSsid} · ${st.ip}`;
}

document.addEventListener('change',e=>{if(e.target.dataset.j!==undefined){posPlaceholders(e.target.dataset.s,e.target.dataset.j);updateAmounts(+e.target.dataset.s);}});
document.addEventListener('input',e=>{const m=/^(vol|entry|target|date)(\d)_\d$/.exec(e.target.id);if(m)updateAmounts(+m[2]);});
document.addEventListener('click',async e=>{
  const s=e.target.dataset.check;if(s===undefined)return;
  const sym=$('sym'+s).value.trim().toUpperCase().replace(/\.BK$/,'');const el=$('chk'+s);
  if(!sym){el.textContent='Enter a symbol first';el.className='check err';return;}
  el.textContent='Checking…';el.className='check';
  try{const r=await fetch('/api/lookup?symbol='+encodeURIComponent(sym)).then(r=>r.json());
    if(r.ok){el.textContent=`${r.name} · ${r.price.toFixed(2)} THB`;el.className='check ok';}
    else{el.textContent=r.error;el.className='check err';}}
  catch(_){el.textContent='Check failed';el.className='check err';}
});
$('alarmVolume').oninput=()=>$('volv').textContent=$('alarmVolume').value;
$('scan').onclick=async()=>{
  $('scan').disabled=true;$('scan').textContent='Scanning…';
  try{const r=await fetch('/api/scan').then(r=>r.json());
    $('nets').innerHTML=r.map(n=>`<option value="${esc(n.ssid)}">${n.rssi} dBm</option>`).join('');
    msg(`Found ${r.length} networks — pick one from the Network fields`);}
  catch(_){msg('Scan failed','err');}
  $('scan').disabled=false;$('scan').textContent='Scan for networks';
};
$('apNow').onclick=async()=>{
  if(!window.confirm('Restart the ticker into setup hotspot mode? This page will stop responding until you join the hotspot.'))return;
  await fetch('/api/setup-mode',{method:'POST'}).catch(()=>{});
  msg('Restarting into setup mode — join the ticker\'s hotspot to continue.','ok');
};
$('test').onclick=()=>fetch('/api/test-alarm?volume='+$('alarmVolume').value,{method:'POST'});
$('save').onclick=async()=>{
  const {out,errors}=gather();
  if(errors.length){msg(errors[0]+(errors.length>1?` (and ${errors.length-1} more)`:''),'err');return;}
  msg('Saving…');
  try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(out)}).then(r=>r.json());
    if(r.ok)msg('Saved. The ticker is restarting…','ok');else msg(r.error,'err');}
  catch(_){msg('Save failed','err');}
};
async function loadSd(){
  const sd=await fetch('/api/sd').then(r=>r.json());
  $('fwVer').textContent=sd.version;
  if(!sd.present){$('sdStatus').textContent=`No SD card inserted. Holidays use the ${sd.holidaySource}. Firmware ${sd.version}.`;return;}
  $('sdStatus').textContent=`Card ready · ${sd.usedMB} of ${sd.totalMB} MB used · backup ${sd.backup?'present':'not yet saved'}`;
  $('sdBody').hidden=false;
  $('sounds').innerHTML=sd.sounds.length?sd.sounds.map(n=>`<span class="inline" style="gap:2px"><button type="button" data-play="${esc(n)}">▶ ${esc(n)}</button><button type="button" data-del="${esc(n)}" title="Delete ${esc(n)}">✕</button></span>`).join('')
    :'<span class="hint" style="margin:0">No sounds on the card yet — the built-in beep is used.</span>';
  $('holSrc').textContent='Active list: '+sd.holidaySource+'.';
  $('holidays').value=await fetch('/api/holidays').then(r=>r.text());
  if(sd.firmware){$('fwPending').hidden=false;$('fwInfo').textContent=`firmware.bin found (${(sd.firmware/1048576).toFixed(2)} MB)`;}
}
async function post(url,body){return fetch(url,{method:'POST',body}).then(r=>r.status===204?{ok:true}:r.json());}
document.addEventListener('click',async e=>{
  const n=e.target.dataset.play;if(n)post('/api/sd/play?file='+encodeURIComponent(n));
  const d=e.target.dataset.del;
  if(d&&window.confirm(`Delete ${d} from the SD card?`)){const r=await post('/api/sd/delete-sound?file='+encodeURIComponent(d));
    msg(r.ok?`Deleted ${d}`:r.error,r.ok?'ok':'err');loadSd();}
});
$('sdBackup').onclick=async()=>{const r=await post('/api/sd/backup');msg(r.ok?'Settings backed up to the SD card':r.error,r.ok?'ok':'err');if(r.ok)loadSd();};
$('sdRestore').onclick=async()=>{
  if(!window.confirm('Replace the current settings with the backup on the SD card? The ticker will restart.'))return;
  const r=await post('/api/sd/restore');msg(r.ok?'Restored. The ticker is restarting…':r.error,r.ok?'ok':'err');};
$('holSave').onclick=async()=>{const r=await post('/api/holidays',$('holidays').value);
  $('holMsg').textContent=r.ok?'Saved':r.error;$('holMsg').className=r.ok?'ok':'err';};
async function upload(kind,input,btn){
  const f=$(input).files[0];if(!f){msg('Choose a file first','err');return;}
  const fd=new FormData();fd.append('file',f,f.name);
  const label=$(btn).textContent;$(btn).disabled=true;$(btn).textContent='Uploading…';
  try{const r=await fetch('/api/sd/upload?kind='+kind,{method:'POST',body:fd}).then(r=>r.json());
    msg(r.ok?`Uploaded ${f.name}`:r.error,r.ok?'ok':'err');if(r.ok){$(input).value='';loadSd();}}
  catch(_){msg('Upload failed','err');}
  $(btn).disabled=false;$(btn).textContent=label;
}
$('soundUp').onclick=()=>upload('sound','soundFile','soundUp');
$('fwUp').onclick=()=>upload('firmware','fwFile','fwUp');
$('fwInstall').onclick=async()=>{
  if(!window.confirm('Install firmware.bin from the SD card now? The ticker restarts and updates (about 30 seconds). Do not power it off.'))return;
  const r=await post('/api/sd/install-firmware');msg(r.ok?'Restarting to install the update…':r.error,r.ok?'ok':'err');};
load().catch(()=>msg('Could not load settings','err'));
loadSd().catch(()=>{$('sdStatus').textContent='Could not read the SD card status';});
</script></body></html>
)HTML";
