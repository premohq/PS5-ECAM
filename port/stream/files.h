// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the remote touch screen's second page, /files (stream.h, SetUploadFolders): files from
// a phone or a computer, sent straight into the app's folders. Where they go (/folders), the files
// to send, and each one's progress as it goes up (PUT /upload), one after another.
#pragma once
namespace ps5stream {
constexpr const char* kFiles = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>Playtendoh files</title>
<style>
*{box-sizing:border-box}html,body{margin:0;background:#090e18;color:#e6edf7;font:15px/1.5 system-ui,sans-serif}[hidden]{display:none!important}
body{min-height:100vh;background:radial-gradient(ellipse at 30% 0%,#254f7a44,transparent 55%),radial-gradient(ellipse at 90% 100%,#ae7c2522,transparent 50%),#090e18}
main{max-width:560px;margin:0 auto;padding:32px 16px 48px}
h1{margin:0 0 4px;font-size:22px;font-weight:600;letter-spacing:.01em}
.lead{margin:0 0 28px;color:#a5b3c7}
label{display:block;margin:0 0 8px;color:#a5b3c7;font-size:13px;font-weight:500;letter-spacing:.08em;text-transform:uppercase}
select,.pick{display:block;width:100%;padding:14px 16px;border:1px solid #ffffff1c;border-radius:14px;background:#ffffff0b;color:#e6edf7;font:inherit}
select:focus,.pick:focus-within{outline:2px solid #80bfff;outline-offset:2px}
.hint{margin:8px 2px 24px;color:#8191a8;font-size:13px}
.pick{position:relative;text-align:center;cursor:pointer;border-style:dashed;padding:22px 16px}
.pick input{position:absolute;inset:0;opacity:0;cursor:pointer}
button{width:100%;margin-top:20px;padding:15px;border:0;border-radius:14px;background:linear-gradient(135deg,#80bfff,#e8c47b);color:#090e18;font:600 16px system-ui,sans-serif;cursor:pointer}
button:disabled{opacity:.4;cursor:default}
ul{list-style:none;margin:24px 0 0;padding:0;display:grid;gap:10px}
li{padding:12px 14px;border:1px solid #ffffff12;border-radius:12px;background:#ffffff07}
.name{overflow-wrap:anywhere}.state{color:#a5b3c7;font-size:13px}.state.done{color:#8fd19e}.state.failed{color:#ff9a8a}
.bar{height:4px;margin-top:8px;border-radius:2px;background:#ffffff14;overflow:hidden}.bar i{display:block;height:100%;width:0;background:#80bfff}
.foot{margin-top:28px;color:#8191a8;font-size:13px}a{color:#80bfff}
</style>
<main>
<h1>Add files</h1>
<p class="lead">Straight into Playtendoh's folders, from this phone or computer. Nothing there is ever replaced.</p>
<label for="to">Where to</label>
<select id="to"></select>
<p class="hint" id="hint"></p>
<label for="files">Files</label>
<div class="pick"><span id="picked">Choose files</span><input id="files" type="file" multiple></div>
<button id="send" disabled>Send</button>
<ul id="list"></ul>
<p class="foot">A new game shows in the library the next time its side opens, or at once with Settings &gt; Game files &gt; Look for games now. <a href="/">The touch screen</a></p>
</main>
<script>
const to=document.querySelector('#to'),hint=document.querySelector('#hint'),files=document.querySelector('#files'),
 picked=document.querySelector('#picked'),send=document.querySelector('#send'),list=document.querySelector('#list');
let folders=[],busy=false;
const size=n=>n>=1e9?(n/1e9).toFixed(1)+' GB':(n/1e6).toFixed(1)+' MB';
function ready(){send.disabled=busy||!files.files.length||!to.value;}
fetch('/folders').then(r=>r.json()).then(list=>{
 folders=list;
 for(const f of list){const o=document.createElement('option');o.value=f.id;o.textContent=f.label;to.append(o);}
 to.onchange();
}).catch(()=>{hint.textContent='The PS5 did not answer: is the remote touch screen still on?';});
to.onchange=()=>{const f=folders.find(f=>f.id===to.value);hint.textContent=f?f.hint:'';ready();};
files.onchange=()=>{const n=files.files.length;picked.textContent=n?(n===1?files.files[0].name:n+' files'):'Choose files';ready();};
function upload(file,folder,row){
 return new Promise(done=>{
  const state=row.querySelector('.state'),bar=row.querySelector('.bar i'),x=new XMLHttpRequest();
  x.open('PUT','/upload?to='+encodeURIComponent(folder)+'&name='+encodeURIComponent(file.name));
  x.upload.onprogress=e=>{if(e.lengthComputable){bar.style.width=(100*e.loaded/e.total)+'%';state.textContent=size(e.loaded)+' of '+size(e.total);}};
  x.onload=()=>{const ok=x.status===201;state.textContent=ok?'Done':x.responseText||('Failed ('+x.status+')');state.className='state '+(ok?'done':'failed');bar.style.width=ok?'100%':bar.style.width;done();};
  x.onerror=x.ontimeout=()=>{state.textContent='The connection was lost: send it again.';state.className='state failed';done();};
  x.send(file);
 });
}
send.onclick=async()=>{
 busy=true;ready();
 const folder=to.value,chosen=[...files.files];
 const rows=chosen.map(file=>{const li=document.createElement('li');li.innerHTML='<div class="name"></div><div class="state">Waiting</div><div class="bar"><i></i></div>';
  li.querySelector('.name').textContent=file.name;list.append(li);return li;});
 for(let i=0;i<chosen.length;i++)await upload(chosen[i],folder,rows[i]);
 files.value='';files.onchange();busy=false;ready();
};
</script>
</html>)HTML";
}
