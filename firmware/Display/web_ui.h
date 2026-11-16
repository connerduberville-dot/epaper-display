// Web UI on the home network (http://<board-ip>/, works from a phone): status, live preview, push a picture,
// firmware update (check GitHub / upload a .bin), settings. One HTML page in flash + a small JSON API.
//
// API (anything that changes state needs the admin password via HTTP basic auth, user "admin", if one is set):
//   GET  /api/status            status JSON            GET  /fb.bin        the 81,600-byte frame on the glass
//   GET  /api/settings          settings JSON          POST /api/settings  JSON body, partial is fine
//   POST /api/action?do=X       check | install | full | pattern | white | clock | reboot | resetwifi
//   POST /update                multipart, field "firmware": an app .bin. Installs and reboots.
//   POST /api/fb                multipart, field "frame": exactly 81,600 bytes. Shown until "clock" or reboot.
//   GET  /api/log               recent log lines
// curl examples:  curl -F firmware=@Display.ino.bin http://10.0.0.4/update
//                 curl -F frame=@frame.bin http://10.0.0.4/api/fb
#pragma once
#include <WebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "framebuffer.h"
#include "settings.h"
#include "logbuf.h"
#include "ota.h"

namespace septa {

struct WebHooks {
  std::function<void(JsonObject)> status;               // fill status JSON
  std::function<String(const char*)> action;            // run an action; returns a message ("" = unknown)
  std::function<void()> settingsChanged;                // apply live
  std::function<uint8_t*()> frameBegin;                 // pushed frame: where to write 81,600 bytes
  std::function<void(bool ok)> frameEnd;                // pushed frame complete (ok) or failed
  const Framebuffer* shown = nullptr;
  Ota* ota = nullptr;
};

static const char UI_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>E-paper display</title>
<style>
:root{--bg:#f4f4f2;--fg:#111;--mut:#666;--card:#fff;--line:#ddd;--acc:#1a4d8f}
@media(prefers-color-scheme:dark){:root{--bg:#141414;--fg:#eee;--mut:#999;--card:#1e1e1e;--line:#333;--acc:#6fa3e8}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}
header{background:#111;color:#fff;padding:14px 16px;display:flex;align-items:baseline;gap:12px;flex-wrap:wrap}
header h1{margin:0;font-size:18px;letter-spacing:.06em;text-transform:uppercase}header small{color:#aaa}
nav{display:flex;border-bottom:1px solid var(--line);background:var(--card);overflow-x:auto}
nav button{flex:1;min-width:80px;padding:12px;border:0;background:none;color:var(--mut);font:inherit;cursor:pointer;border-bottom:3px solid transparent}
nav button.on{color:var(--fg);border-color:var(--acc);font-weight:600}
main{max-width:1100px;margin:0 auto;padding:16px}section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin-bottom:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}
dl{margin:0;display:grid;grid-template-columns:auto 1fr;gap:4px 14px}dt{color:var(--mut)}dd{margin:0;overflow-wrap:anywhere}
.ok{color:#1d8a3a}.bad{color:#c62828}.warn{color:#b26a00}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:13px}
input[type=number],input[type=text],input[type=password],select{width:100%;padding:8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit}
input[type=checkbox]{transform:scale(1.3);margin-right:8px}
button.b{padding:9px 16px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--fg);font:inherit;cursor:pointer;margin:4px 6px 4px 0}
button.b.p{background:var(--acc);border-color:var(--acc);color:#fff}button.b.d{border-color:#c62828;color:#c62828}
canvas{width:100%;height:auto;background:#fff;border:1px solid var(--line);border-radius:6px;image-rendering:pixelated}
pre{background:#0d0d0d;color:#ddd;padding:12px;border-radius:8px;overflow:auto;max-height:50vh;font:12px/1.4 ui-monospace,monospace;white-space:pre-wrap}
progress{width:100%;height:14px}
#msg{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#111;color:#fff;padding:10px 18px;border-radius:8px;opacity:0;transition:.3s;pointer-events:none;max-width:90vw}
#msg.on{opacity:1}h2{font-size:15px;margin:0 0 10px;text-transform:uppercase;letter-spacing:.05em;color:var(--mut)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.hint{color:var(--mut);font-size:13px}
</style></head><body>
<header><h1>E-paper display</h1><small id=hdr>connecting...</small></header>
<nav><button data-t=status class=on>Status</button><button data-t=preview>Picture</button><button data-t=update>Update</button><button data-t=settings>Settings</button><button data-t=log>Log</button></nav>
<main>
<section id=status class=on>
 <div class=grid>
  <div class=card><h2>Firmware</h2><dl id=sfw></dl></div>
  <div class=card><h2>Display</h2><dl id=sdisp></dl></div>
  <div class=card><h2>Network</h2><dl id=snet></dl></div>
  <div class=card><h2>System</h2><dl id=ssys></dl></div>
 </div>
</section>
<section id=preview>
 <div class=card><h2>On the glass now</h2><div class=row><button class="b" onclick="loadFb()">Refresh preview</button><span class=hint id=fbinfo></span></div>
 <br><canvas id=cv width=1360 height=480></canvas></div>
 <div class=card><h2>Show a picture</h2>
  <p class=hint>Pick any image. It's cropped to 1360 x 480, converted to black and white here in the browser, and sent to the board. It stays until you press "Back to clock" or the board restarts.</p>
  <div class=row><input type=file id=img accept="image/*"><label style="margin:0"><input type=checkbox id=dither checked> dither (photos)</label>
  <label style="margin:0">threshold <input type=number id=thr value=128 min=1 max=254 style="width:5em"></label></div>
  <br><canvas id=pv width=1360 height=480></canvas>
  <div class=row><button class="b p" onclick="sendImg()">Send to display</button><button class=b onclick="act('clock')">Back to clock</button></div></div>
</section>
<section id=update>
 <div class=card><h2>From GitHub</h2><dl id=uinfo></dl>
  <div class=row style="margin-top:10px"><button class=b onclick="act('check')">Check now</button><button class="b p" onclick="act('install','Download and install the latest firmware from GitHub? The board restarts.')">Install latest</button></div>
  <p class=hint>The board also checks by itself on the interval set in Settings. Development builds (build 0) never auto-install.</p></div>
 <div class=card><h2>Upload a .bin</h2>
  <p class=hint>An app image built for this board (e.g. <code>Display.ino.bin</code>). The board installs it and restarts. If the new firmware can't stay up on Wi-Fi for a minute, it switches back to this one by itself.</p>
  <input type=file id=fw accept=".bin"><br><br><progress id=fwp value=0 max=100 style="display:none"></progress>
  <button class="b p" onclick="uploadFw()">Upload and install</button></div>
</section>
<section id=settings>
 <form id=sform class=card onsubmit="return saveSettings(event)">
  <h2>Firmware updates</h2>
  <label>Update manifest URL (https)</label><input type=text name=updateUrl placeholder="https://github.com/OWNER/REPO/releases/download/ota/manifest.json">
  <div class=grid><div><label>Check every (minutes)</label><input type=number name=checkMin min=1 max=1440></div>
  <div><label style="margin-top:34px"><input type=checkbox name=autoUpdate> Install updates automatically</label></div></div>
  <h2 style="margin-top:18px">Screen</h2>
  <div class=grid><div><label>Show</label><select name=screen><option value=0>Home screen: clock, weather, sky (landscape)</option><option value=1>SEPTA subway map (portrait)</option></select></div>
  <div><label>When hung vertically</label><select name=turnLeft><option value=0>Turned clockwise (panel's left edge at the top)</option><option value=1>Turned counter-clockwise (right edge at the top)</option></select></div></div>
  <h2 style="margin-top:18px">Home screen</h2>
  <p class=hint>Clock, weather and sky. The forecast comes from Open-Meteo (free, no key). The location is also used for sunrise and sunset.</p>
  <div class=grid><div><label>Your name (greeting)</label><input type=text name=name maxlength=23></div>
  <div><label style="margin-top:34px"><input type=checkbox name=weather> Show the weather forecast</label></div></div>
  <div class=grid><div><label>Latitude</label><input type=number name=lat step=any min=-90 max=90></div>
  <div><label>Longitude</label><input type=number name=lon step=any min=-180 max=180></div></div>
  <label><input type=checkbox name=metric> Metric units (&deg;C, km/h)</label>
  <h2 style="margin-top:18px">Content</h2>
  <p class=hint>Optional. A URL serving a raw 81,600-byte frame (1 bit per pixel, 1 = black). Leave empty to show the home screen.</p>
  <label>Content URL (http or https)</label><input type=text name=contentUrl>
  <div class=grid><div><label>Fetch every (seconds)</label><input type=number name=contentSec min=30 max=43200></div>
  <div><label>Time zone (POSIX TZ)</label><input type=text name=tz maxlength=47></div></div>
  <h2 style="margin-top:18px">Refresh</h2>
  <div class=grid><div><label>Full refresh after N partial updates</label><input type=number name=fullEveryN min=1 max=200></div>
  <div><label>...or at least every (minutes)</label><input type=number name=fullMaxAgeMin min=5 max=1440></div></div>
  <label><input type=checkbox name=wifiSleep> Wi-Fi modem sleep (saves power)</label>
  <h2 style="margin-top:18px">Admin password</h2>
  <p class=hint id=pwstate></p>
  <label>New password (user name "admin"). Leave empty to keep the current one.</label><input type=password name=adminPass autocomplete=new-password>
  <label><input type=checkbox id=clearpw> Remove the password</label>
  <br><button class="b p">Save settings</button><button type=button class=b onclick="resetSettings()">Reset to defaults</button>
 </form>
 <div class=card><h2>Panel and system</h2>
  <button class=b onclick="act('full')">Full refresh</button><button class=b onclick="act('pattern')">Test pattern</button>
  <button class=b onclick="act('reboot','Restart the board?')">Restart</button>
  <button class="b d" onclick="act('resetwifi','Forget Wi-Fi and restart into setup mode?')">Forget Wi-Fi</button></div>
</section>
<section id=log><div class=card><div class=row><button class=b onclick="loadLog()">Refresh</button></div><pre id=logt>...</pre></div></section>
</main><div id=msg></div>
<script>
const $=s=>document.querySelector(s);
function toast(t){const m=$('#msg');m.textContent=t;m.classList.add('on');setTimeout(()=>m.classList.remove('on'),3000)}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button,section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.t).classList.add('on');if(b.dataset.t=='preview')loadFb();if(b.dataset.t=='log')loadLog()});
function dl(el,rows){el.innerHTML=rows.map(([k,v,c])=>`<dt>${k}</dt><dd class="${c||''}">${v}</dd>`).join('')}
function fmtS(s){if(s==null)return 'never';if(s<90)return s+' s';if(s<5400)return Math.round(s/60)+' min';if(s<172800)return (s/3600).toFixed(1)+' h';return (s/86400).toFixed(1)+' d'}
async function loadStatus(){try{const s=await (await fetch('/api/status')).json(),o=s.ota;
 $('#hdr').textContent=s.time+' | '+s.ip;
 const fw=[['Version',o.version+' (build '+o.build+(o.build==0?', dev':'')+')'],['Slot',o.slot+(o.pending?' (new, confirming...)':''),o.pending?'warn':''],
  ['Latest on GitHub',o.latestBuild>=0?o.latestVersion+' (build '+o.latestBuild+')':'unknown'],['Last check',fmtS(o.lastCheckS)+' ago: '+o.result,/fail|error|mismatch|HTTP|invalid|bad/i.test(o.result)?'bad':'']];
 dl($('#sfw'),fw);dl($('#uinfo'),fw);
 dl($('#sdisp'),[['Showing',s.mode],['Last update',s.epd.kind+' in '+s.epd.ms+' ms'],['Panel',s.epd.busyTimeout?'BUSY TIMEOUT (not responding)':'ok',s.epd.busyTimeout?'bad':'ok'],['Partials since full',s.partials],['Content',s.content||'-']]);
 dl($('#snet'),[['Wi-Fi',s.ssid],['Signal',s.rssi+' dBm',s.rssi>-67?'ok':s.rssi>-75?'warn':'bad'],['IP',s.ip]]);
 dl($('#ssys'),[['Uptime',fmtS(s.uptime)],['Free heap',Math.round(s.heap/1024)+' KB (min '+Math.round(s.minHeap/1024)+')'],['Reset reason',s.reset]]);
}catch(e){$('#hdr').textContent='board unreachable'}}
function draw1bit(c,b){const x=c.getContext('2d'),im=x.createImageData(1360,480),d=im.data;for(let i=0,p=0;i<b.length;i++){const v=b[i];for(let k=7;k>=0;k--,p+=4){const w=(v>>k)&1?0:255;d[p]=d[p+1]=d[p+2]=w;d[p+3]=255}}x.putImageData(im,0,0)}
async function loadFb(){try{const b=new Uint8Array(await (await fetch('/fb.bin',{cache:'no-store'})).arrayBuffer());draw1bit($('#cv'),b);$('#fbinfo').textContent='updated '+new Date().toLocaleTimeString()}catch(e){$('#fbinfo').textContent='preview failed'}}
let frame=null;
function convert(){const f=$('#img').files[0];if(!f)return;const im=new Image();im.onload=()=>{const W=1360,H=480,c=document.createElement('canvas');c.width=W;c.height=H;const x=c.getContext('2d');x.fillStyle='#fff';x.fillRect(0,0,W,H);
 const s=Math.max(W/im.width,H/im.height),w=im.width*s,h=im.height*s;x.drawImage(im,(W-w)/2,(H-h)/2,w,h);const d=x.getImageData(0,0,W,H).data,g=new Float32Array(W*H);
 for(let i=0;i<W*H;i++)g[i]=0.299*d[i*4]+0.587*d[i*4+1]+0.114*d[i*4+2];const t=+$('#thr').value,dith=$('#dither').checked;frame=new Uint8Array(170*H);
 for(let y=0;y<H;y++)for(let X=0;X<W;X++){const i=y*W+X,o=g[i],n=o<t?0:255,e=o-n;if(!n)frame[y*170+(X>>3)]|=0x80>>(X&7);
  if(dith){if(X+1<W)g[i+1]+=e*7/16;if(y+1<H){if(X>0)g[i+W-1]+=e*3/16;g[i+W]+=e*5/16;if(X+1<W)g[i+W+1]+=e/16}}}
 draw1bit($('#pv'),frame)};im.src=URL.createObjectURL(f)}
['#img','#dither','#thr'].forEach(s=>$(s).addEventListener('change',convert));
async function sendImg(){if(!frame){toast('Pick an image first');return}const fd=new FormData();fd.append('frame',new Blob([frame]),'frame.bin');toast('Sending...');
 const r=await fetch('/api/fb',{method:'POST',body:fd});toast(r.ok?'On the display':'Failed: '+await r.text());setTimeout(loadFb,1500)}
function uploadFw(){const f=$('#fw').files[0];if(!f){toast('Choose a .bin first');return}if(!confirm('Install '+f.name+' ('+Math.round(f.size/1024)+' KB)? The board restarts.'))return;
 const fd=new FormData();fd.append('firmware',f,f.name);const x=new XMLHttpRequest(),p=$('#fwp');p.style.display='';x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded/e.total*100};
 x.onload=()=>{toast(x.status==200?'Installed. Restarting...':'Failed: '+x.responseText);if(x.status==200)setTimeout(()=>location.reload(),15000)};x.onerror=()=>toast('Upload failed');x.open('POST','/update');x.send(fd)}
async function loadSettings(){const s=await (await fetch('/api/settings')).json(),f=$('#sform');for(const k in s){const el=f.elements[k];if(!el)continue;if(el.type=='checkbox')el.checked=!!s[k];else if(el.type!='password')el.value=s[k]}
 $('#pwstate').textContent=s.hasAdminPass?'A password is set: changes from this page and uploads ask for it.':'No password: anyone on your Wi-Fi can change this board.'}
async function saveSettings(ev){ev.preventDefault();const f=$('#sform'),o={};for(const el of f.elements){if(!el.name)continue;if(el.type=='password'){if(el.value)o[el.name]=el.value;continue}o[el.name]=el.type=='checkbox'?el.checked:(el.type=='number'?+el.value:el.value)}
 if($('#clearpw').checked)o.adminPass='';const r=await fetch('/api/settings',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(o)});toast(r.ok?'Settings saved':'Save failed: '+r.status);f.elements.adminPass.value='';$('#clearpw').checked=false;loadSettings();loadStatus();return false}
async function resetSettings(){if(!confirm('Reset all settings to defaults?'))return;const r=await fetch('/api/settings/reset',{method:'POST'});toast(r.ok?'Defaults restored':'Failed');loadSettings();loadStatus()}
async function act(a,conf){if(conf&&!confirm(conf))return;toast('Working...');const r=await fetch('/api/action?do='+a,{method:'POST'});toast(await r.text());setTimeout(()=>{loadStatus();if($('#preview').classList.contains('on'))loadFb()},1000)}
async function loadLog(){try{$('#logt').textContent=await (await fetch('/api/log')).text();$('#logt').scrollTop=1e9}catch(e){}}
loadStatus();loadSettings();setInterval(loadStatus,5000);
</script></body></html>)HTML";

class WebUi {
 public:
  void begin(WebServer& srv, Settings* st, WebHooks hooks) {
    s_ = &srv; st_ = st; h_ = hooks;
    srv.on("/", HTTP_GET, [this]() { s_->send_P(200, "text/html", UI_HTML); });
    srv.on("/api/status", HTTP_GET, [this]() {
      JsonDocument d; JsonObject o = d.to<JsonObject>();
      if (h_.status) h_.status(o);
      if (h_.ota) h_.ota->toJson(o["ota"].to<JsonObject>());
      sendJson(d);
    });
    srv.on("/api/settings", HTTP_GET, [this]() { JsonDocument d; st_->toJson(d.to<JsonObject>()); sendJson(d); });
    srv.on("/api/settings", HTTP_POST, [this]() {
      if (!authed()) return;
      JsonDocument d;
      if (deserializeJson(d, s_->arg("plain")) || !d.is<JsonObject>()) { s_->send(400, "text/plain", "bad json"); return; }
      st_->fromJson(d.as<JsonObjectConst>()); st_->save();
      if (h_.settingsChanged) h_.settingsChanged();
      LOG("[cfg] settings saved\n");
      s_->send(200, "text/plain", "ok");
    });
    srv.on("/api/settings/reset", HTTP_POST, [this]() {
      if (!authed()) return;
      Settings::reset(); *st_ = Settings(); if (h_.settingsChanged) h_.settingsChanged();
      LOG("[cfg] settings reset to defaults\n"); s_->send(200, "text/plain", "ok");
    });
    srv.on("/fb.bin", HTTP_GET, [this]() {
      if (!h_.shown) { s_->send(503, "text/plain", "no framebuffer"); return; }
      s_->sendHeader("Cache-Control", "no-store");
      s_->send_P(200, "application/octet-stream", (PGM_P)h_.shown->px, FB_BYTES);
    });
    srv.on("/api/log", HTTP_GET, [this]() {
      static char buf[LogRing::CAP + 1]; LOGR.read(buf, sizeof buf);
      s_->send(200, "text/plain", buf);
    });
    srv.on("/api/action", HTTP_POST, [this]() {
      if (!authed()) return;
      String a = s_->arg("do");
      if (a == "reboot" || a == "resetwifi") {   // reply first: these don't come back
        s_->send(200, "text/plain", a == "reboot" ? "Restarting" : "Restarting into Wi-Fi setup"); delay(300);
      }
      String msg = h_.action ? h_.action(a.c_str()) : String();
      s_->send(msg.length() ? 200 : 400, "text/plain", msg.length() ? msg : ("Unknown action: " + a));
    });

    // Firmware upload. The upload callback runs per chunk; the request callback runs when it's done.
    srv.on("/update", HTTP_POST, [this]() {
      if (uploadDenied_) { s_->requestAuthentication(BASIC_AUTH, "e-paper", "admin password required"); return; }
      const char* err = h_.ota->uploadError();
      if (*err || !uploadOk_) { s_->send(500, "text/plain", *err ? err : "upload incomplete"); return; }
      s_->send(200, "text/plain", "Installed. Restarting.");
      delay(500); ESP.restart();
    }, [this]() {
      HTTPUpload& up = s_->upload();
      if (up.status == UPLOAD_FILE_START) {
        uploadOk_ = false; uploadDenied_ = !passOk();
        if (uploadDenied_) return;
        LOG("[ota] LAN upload: %s\n", up.filename.c_str());
        h_.ota->uploadBegin(0);
      } else if (uploadDenied_) {
        return;
      } else if (up.status == UPLOAD_FILE_WRITE) {
        h_.ota->uploadWrite(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END) {
        uploadOk_ = h_.ota->uploadEnd();
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        h_.ota->uploadAbort();
      }
    });

    // Push a raw frame (81,600 bytes) straight into the "next" framebuffer.
    srv.on("/api/fb", HTTP_POST, [this]() {
      if (uploadDenied_) { s_->requestAuthentication(BASIC_AUTH, "e-paper", "admin password required"); return; }
      bool ok = frameDst_ && frameGot_ == FB_BYTES;
      if (h_.frameEnd) h_.frameEnd(ok);
      frameDst_ = nullptr;
      if (ok) s_->send(200, "text/plain", "ok");
      else { char m[64]; snprintf(m, sizeof m, "need exactly %u bytes, got %u", (unsigned)FB_BYTES, (unsigned)frameGot_); s_->send(400, "text/plain", m); }
    }, [this]() {
      HTTPUpload& up = s_->upload();
      if (up.status == UPLOAD_FILE_START) {
        uploadDenied_ = !passOk(); frameGot_ = 0;
        frameDst_ = (!uploadDenied_ && h_.frameBegin) ? h_.frameBegin() : nullptr;
      } else if (up.status == UPLOAD_FILE_WRITE && frameDst_) {
        size_t n = up.currentSize;
        if (frameGot_ + n <= FB_BYTES) memcpy(frameDst_ + frameGot_, up.buf, n);
        frameGot_ += n;   // more than FB_BYTES is reported as an error at the end
      }
    });
    srv.onNotFound([this]() { s_->send(404, "text/plain", "not found"); });
    srv.begin();
  }

 private:
  WebServer* s_ = nullptr; Settings* st_ = nullptr; WebHooks h_;
  bool uploadOk_ = false, uploadDenied_ = false;
  uint8_t* frameDst_ = nullptr; size_t frameGot_ = 0;

  bool passOk() { return !st_->adminPass[0] || s_->authenticate("admin", st_->adminPass); }
  bool authed() {
    if (passOk()) return true;
    s_->requestAuthentication(BASIC_AUTH, "e-paper", "admin password required");
    return false;
  }
  void sendJson(JsonDocument& d) { String out; serializeJson(d, out); s_->send(200, "application/json", out); }
};

}  // namespace septa
