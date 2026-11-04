// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the remote touch screen's page (stream.h), from dolphin78's prototype (#38): the
// picture, filling the browser, and its touches; a quiet splash while there is no picture, with the
// way to the files page (files.h).
#pragma once
namespace ps5stream {
constexpr const char* kBrowser = R"HTML(<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover"><title>Playtendoh touch screen</title>
<style>
*{box-sizing:border-box}html,body{margin:0;background:#090e18;width:100%;height:100%;overflow:hidden}[hidden]{display:none!important}
#screen{display:block;width:100vw;height:100vh;height:100dvh;object-fit:contain;touch-action:none;-webkit-touch-callout:none;-webkit-user-select:none;user-select:none;-webkit-user-drag:none}
#splash{position:fixed;inset:0;display:grid;place-items:center;background:radial-gradient(ellipse at 30% 25%,#254f7a44,transparent 55%),radial-gradient(ellipse at 80% 80%,#ae7c2526,transparent 50%),#090e18}
.splash-content{display:flex;flex-direction:column;align-items:center;gap:32px}
.mark{position:relative;width:152px;height:152px;display:grid;place-items:center;border:1px solid #ffffff12;border-radius:42px;background:linear-gradient(145deg,#ffffff0b,#ffffff02);box-shadow:0 24px 80px #0005,inset 0 1px #ffffff0a}
.mark:before{content:"";position:absolute;inset:-26px;border:1px solid #7bb9ff0b;border-radius:62px}
.mark svg{width:88px;height:88px;filter:drop-shadow(0 0 18px #65aeff33)}
.status{margin:0;color:#a5b3c7;font:500 13px/1.5 system-ui,sans-serif;letter-spacing:.12em}
.files{color:#80bfff;font:500 13px/1.5 system-ui,sans-serif;letter-spacing:.08em;text-decoration:none;padding:10px 18px;border:1px solid #80bfff33;border-radius:999px}
.dots{display:flex;gap:7px}.dots i{width:4px;height:4px;border-radius:50%;background:#8bc2ff;animation:breathe 2s ease-in-out infinite}.dots i:nth-child(2){animation-delay:.2s}.dots i:nth-child(3){animation-delay:.4s;background:#e8c47b}
@keyframes breathe{0%,80%,100%{opacity:.25;transform:translateY(0)}40%{opacity:1;transform:translateY(-3px)}}
@media(prefers-reduced-motion:reduce){.dots i{animation:none;opacity:.6}}
</style>
<img id="screen" draggable="false" alt="Second screen" hidden>
<div id="splash" role="status" aria-live="polite"><div class="splash-content">
<div class="mark"><svg viewBox="0 0 88 88" fill="none" aria-hidden="true"><defs><linearGradient id="glow" x1="12" y1="16" x2="78" y2="76" gradientUnits="userSpaceOnUse"><stop stop-color="#80bfff"/><stop offset="1" stop-color="#e8c47b"/></linearGradient></defs><rect x="9" y="21" width="70" height="46" rx="14" stroke="url(#glow)" stroke-width="2"/><rect x="26" y="30" width="36" height="28" rx="5" fill="#80bfff0a" stroke="url(#glow)" stroke-width="1.5"/><path d="M15 44h6m-3-3v6" stroke="#80bfff" stroke-width="1.5" stroke-linecap="round"/><circle cx="70" cy="40" r="1.5" fill="#e8c47b"/><circle cx="70" cy="48" r="1.5" fill="#e8c47b"/></svg></div>
<p class="status">Waiting for video</p><div class="dots" aria-hidden="true"><i></i><i></i><i></i></div>
<a class="files" href="/files">Add files to the PS5</a>
</div></div>
<script>
const screen=document.querySelector('#screen'),splash=document.querySelector('#splash');
// a long press on the game is a touch: never Safari's image actions (Share, Save Image), nor a drag (#38, dolphin78)
screen.oncontextmenu=screen.ondragstart=e=>e.preventDefault();
let active=null,x=0,y=0,inputSocket=null,videoSocket=null,moveScheduled=false,videoAvailable=false,frameTimeout=null;
function showSplash(){clearTimeout(frameTimeout);frameTimeout=null;videoAvailable=false;screen.hidden=true;splash.hidden=false;release();}
function showVideo(){videoAvailable=true;screen.hidden=false;splash.hidden=true;clearTimeout(frameTimeout);frameTimeout=setTimeout(showSplash,4000);}
const websocketURL=path=>(location.protocol==='https:'?'wss://':'ws://')+location.host+path;
function touch(down){
 if(!inputSocket||inputSocket.readyState!==1)return false;
 // Reconnect instead of accumulating old input behind a stalled connection.
 if(inputSocket.bufferedAmount>1024){inputSocket.close();return false;}
 inputSocket.send((down?1:0)+','+x+','+y);return true;
}
function connectInput(){
 if(document.hidden||inputSocket)return;
 const socket=new WebSocket(websocketURL('/input'));inputSocket=socket;
 socket.onopen=()=>{if(inputSocket!==socket)return;touch(false);};
 socket.onclose=()=>{if(inputSocket!==socket)return;inputSocket=null;active=null;if(!document.hidden)setTimeout(connectInput,1000);};
 socket.onerror=()=>socket.close();
}
function connectVideo(){
 if(document.hidden||videoSocket)return;
 const socket=new WebSocket(websocketURL('/video'));videoSocket=socket;socket.binaryType='blob';
 socket.onopen=()=>{socket.send('ready');};
 socket.onmessage=event=>{
  if(videoSocket!==socket||document.hidden)return;
  const url=URL.createObjectURL(event.data);
  const decoded=valid=>{URL.revokeObjectURL(url);screen.onload=screen.onerror=null;if(videoSocket!==socket||document.hidden)return;if(valid)showVideo();else showSplash();if(socket.readyState===1)socket.send('ready');};
  screen.onload=()=>decoded(true);screen.onerror=()=>decoded(false);screen.src=url;
 };
 socket.onclose=()=>{if(videoSocket!==socket)return;videoSocket=null;showSplash();if(!document.hidden)setTimeout(connectVideo,1000);};
 socket.onerror=()=>socket.close();
}
function disconnect(){showSplash();const input=inputSocket,video=videoSocket;inputSocket=videoSocket=null;if(input)input.close();if(video)video.close();}


function position(e){const r=screen.getBoundingClientRect(),aspect=(screen.naturalWidth||854)/(screen.naturalHeight||480),w=Math.min(r.width,r.height*aspect),h=w/aspect;x=Math.max(0,Math.min(1,(e.clientX-r.left-(r.width-w)/2)/w));y=Math.max(0,Math.min(1,(e.clientY-r.top-(r.height-h)/2)/h));}
screen.onpointerdown=e=>{if(!videoAvailable||active!==null||!screen.naturalWidth||inputSocket?.readyState!==1)return;active=e.pointerId;screen.setPointerCapture(active);position(e);touch(true);e.preventDefault();};
screen.onpointermove=e=>{if(e.pointerId===active){position(e);if(!moveScheduled){moveScheduled=true;requestAnimationFrame(()=>{moveScheduled=false;if(active!==null)touch(true);});}}};
function release(){if(active!==null){active=null;touch(false);}}
screen.onpointerup=screen.onpointercancel=screen.onlostpointercapture=release;
window.addEventListener('pagehide',disconnect);document.addEventListener('visibilitychange',()=>{if(document.hidden)disconnect();else{connectInput();connectVideo();}});
setInterval(()=>{if(active!==null)touch(true);},250);
connectInput();connectVideo();
</script></html>)HTML";
}
