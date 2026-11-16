// Web UI served on the home network: status, live panel preview, settings, developer tools.
// Single HTML page (in flash) + small JSON API. The sketch supplies the hooks; this file owns the routes.
#pragma once
#include <WebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "framebuffer.h"
#include "settings.h"
#include "logbuf.h"

namespace septa {

struct WebHooks {
  std::function<void(JsonObject)> status;                 // fill status JSON
  std::function<void(JsonArray)>  feed;                   // last fetched trains
  std::function<bool(const char*)> action;                // "poll","full","pattern","white","reboot","resetwifi" -> handled?
  std::function<void()> settingsChanged;                  // apply live (sleep, cpu)
  std::function<bool(JsonArrayConst)> simulate;           // developer: inject trains (empty array = off)
  const Framebuffer* shown = nullptr;                     // what the panel displays
};

static const char UI_HTML[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>SEPTA Board</title>
<style>
:root{--bg:#f4f4f2;--fg:#111;--mut:#666;--card:#fff;--line:#ddd;--acc:#1a4d8f}
@media(prefers-color-scheme:dark){:root{--bg:#141414;--fg:#eee;--mut:#999;--card:#1e1e1e;--line:#333;--acc:#6fa3e8}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}
header{background:#111;color:#fff;padding:14px 16px;display:flex;align-items:baseline;gap:12px;flex-wrap:wrap}
header h1{margin:0;font-size:18px;letter-spacing:.06em;text-transform:uppercase}header small{color:#aaa}
nav{display:flex;border-bottom:1px solid var(--line);background:var(--card);overflow-x:auto}
nav button{flex:1;min-width:90px;padding:12px;border:0;background:none;color:var(--mut);font:inherit;cursor:pointer;border-bottom:3px solid transparent}
nav button.on{color:var(--fg);border-color:var(--acc);font-weight:600}
main{max-width:1100px;margin:0 auto;padding:16px}section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin-bottom:14px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px}
dl{margin:0;display:grid;grid-template-columns:auto 1fr;gap:4px 14px}dt{color:var(--mut)}dd{margin:0;font-variant-numeric:tabular-nums}
.ok{color:#1d8a3a}.bad{color:#c62828}.warn{color:#b26a00}
label{display:block;margin:10px 0 4px;color:var(--mut);font-size:13px}
input[type=number],input[type=text]{width:100%;padding:8px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);font:inherit}
input[type=checkbox]{transform:scale(1.3);margin-right:8px}
button.b{padding:9px 16px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--fg);font:inherit;cursor:pointer;margin:4px 6px 4px 0}
button.b.p{background:var(--acc);border-color:var(--acc);color:#fff}button.b.d{border-color:#c62828;color:#c62828}
canvas{width:100%;height:auto;background:#fff;border:1px solid var(--line);border-radius:6px;image-rendering:pixelated;image-rendering:crisp-edges}
pre{background:#0d0d0d;color:#ddd;padding:12px;border-radius:8px;overflow:auto;max-height:50vh;font:12px/1.4 ui-monospace,monospace;white-space:pre-wrap}
textarea{width:100%;min-height:120px;font:12px ui-monospace,monospace;border:1px solid var(--line);border-radius:6px;padding:8px;background:var(--bg);color:var(--fg)}
#msg{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#111;color:#fff;padding:10px 18px;border-radius:8px;opacity:0;transition:.3s;pointer-events:none}
#msg.on{opacity:1}h2{font-size:15px;margin:0 0 10px;text-transform:uppercase;letter-spacing:.05em;color:var(--mut)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.hint{color:var(--mut);font-size:13px}
</style></head><body>
<header><h1>Fort Washington board</h1><small id=hdr>connecting...</small></header>
<nav><button data-t=status class=on>Status</button><button data-t=preview>Preview</button><button data-t=settings>Settings</button><button data-t=dev id=devtab style="display:none">Developer</button></nav>
<main>
<section id=status class=on>
 <div class=grid>
  <div class=card><h2>Feed</h2><dl id=sfeed></dl></div>
  <div class=card><h2>Network</h2><dl id=snet></dl></div>
  <div class=card><h2>Display</h2><dl id=sdisp></dl></div>
  <div class=card><h2>System</h2><dl id=ssys></dl></div>
 </div>
 <div class=card><h2>Upcoming trains</h2><div id=strains class=hint>...</div></div>
</section>
<section id=preview>
 <div class=card><div class=row><button class="b p" onclick="loadFb()">Refresh preview</button><label style="margin:0"><input type=checkbox id=auto checked> auto every 10 s</label><span class=hint id=fbinfo></span></div>
 <br><canvas id=cv width=1360 height=480></canvas>
 <p class=hint>This is the exact 1360 x 480 1-bit framebuffer the panel was last sent. Black = ink.</p></div>
</section>
<section id=settings>
 <form id=sform class=card onsubmit="return saveSettings(event)">
  <h2>Polling and refresh</h2>
  <div class=grid>
   <div><label>Poll SEPTA every (seconds)</label><input type=number name=pollSec min=15 max=3600></div>
   <div><label>Full refresh after N partial updates</label><input type=number name=fullEveryN min=1 max=200></div>
   <div><label>...or at least every (minutes)</label><input type=number name=fullMaxAgeMin min=5 max=1440></div>
   <div><label>Show FEED UNAVAILABLE after (minutes without data)</label><input type=number name=staleMin min=2 max=240></div>
  </div>
  <h2 style="margin-top:18px">Night mode</h2><p class=hint>Polls less often overnight. Set start = end to disable.</p>
  <div class=grid>
   <div><label>Start hour (0-23)</label><input type=number name=nightStart min=0 max=23></div>
   <div><label>End hour (0-23)</label><input type=number name=nightEnd min=0 max=23></div>
   <div><label>Night poll interval (seconds)</label><input type=number name=nightPollSec min=60 max=3600></div>
  </div>
  <h2 style="margin-top:18px">Route</h2><p class=hint>SEPTA's own station spellings, e.g. "Ft Washington", "Suburban Station".</p>
  <div class=grid>
   <div><label>Origin</label><input type=text name=origin maxlength=31></div>
   <div><label>Destination</label><input type=text name=dest maxlength=31></div>
  </div>
  <h2 style="margin-top:18px">Power</h2>
  <label><input type=checkbox name=wifiSleep> Wi-Fi modem sleep (recommended; saves ~50 mA, adds a few ms latency)</label>
  <label><input type=checkbox name=cpu80> Run CPU at 80 MHz instead of 160 (slightly slower HTTPS)</label>
  <h2 style="margin-top:18px">Developer</h2>
  <label><input type=checkbox name=devMode> Enable developer mode (extra tab with logs, test patterns, simulation)</label>
  <br><button class="b p">Save settings</button><button type=button class=b onclick="resetSettings()">Reset to defaults</button>
  <span class=hint>Saved immediately; no reboot needed.</span>
 </form>
 <div class=card><h2>Wi-Fi</h2><button class="b d" onclick="act('resetwifi','Forget Wi-Fi and restart into setup mode?')">Forget Wi-Fi and restart</button></div>
</section>
<section id=dev>
 <div class=card><h2>Panel</h2>
  <button class=b onclick="act('poll')">Poll SEPTA now</button>
  <button class=b onclick="act('full')">Force full refresh</button>
  <button class=b onclick="act('pattern')">Test pattern</button>
  <button class=b onclick="act('white')">Clear to white</button>
  <p class=hint>Test pattern labels the LEFT and RIGHT halves and draws a border: use it to check half order and polarity when the panel is first wired.</p></div>
 <div class=card><h2>Simulate trains</h2><p class=hint>Overrides the live feed until turned off. Format: one train per line as <code>number, H:MMAM, delay</code> (delay e.g. "On time", "12 mins", "Cancelled").</p>
  <textarea id=sim>5319, 8:03PM, On time
5323, 9:03PM, 14 mins
5327, 10:03PM, Cancelled</textarea>
  <div class=row><button class="b p" onclick="simOn()">Apply simulation</button><button class=b onclick="simOff()">Back to live feed</button><span class=hint id=simstate></span></div></div>
 <div class=card><h2>Last feed (raw)</h2><pre id=feed>...</pre></div>
 <div class=card><h2>Log</h2><div class=row><label style="margin:0"><input type=checkbox id=logauto checked> follow</label><button class=b onclick="loadLog()">Refresh</button></div><pre id=log>...</pre></div>
 <div class=card><h2>System</h2><button class="b d" onclick="act('reboot','Reboot the board?')">Reboot</button></div>
</section>
</main><div id=msg></div>
<script>
const $=s=>document.querySelector(s);let S=null,dev=false;
function toast(t){const m=$('#msg');m.textContent=t;m.classList.add('on');setTimeout(()=>m.classList.remove('on'),2200)}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.remove('on'));document.querySelectorAll('section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.t).classList.add('on');if(b.dataset.t=='preview')loadFb();if(b.dataset.t=='dev'){loadLog();loadFeed()}});
function dl(el,rows){el.innerHTML=rows.map(([k,v,c])=>`<dt>${k}</dt><dd class="${c||''}">${v}</dd>`).join('')}
function fmtS(s){if(s<90)return s+' s';if(s<5400)return Math.round(s/60)+' min';if(s<172800)return (s/3600).toFixed(1)+' h';return (s/86400).toFixed(1)+' d'}
async function loadStatus(){try{const r=await fetch('/api/status');const s=await r.json();S=s;
 $('#hdr').textContent=s.time+' | '+s.ip;
 dl($('#sfeed'),[['Feed',s.feed,s.feed=='ok'?'ok':'bad'],['Source',s.sim?'SIMULATED':'live SEPTA',s.sim?'warn':''],['Fetched',s.fetched+' trains'],['Shown',s.shown],['Last good',s.lastGoodS!=null?fmtS(s.lastGoodS)+' ago':'never'],['Next poll',fmtS(s.nextPollS)+(s.night?' (night mode)':'')]]);
 dl($('#snet'),[['SSID',s.ssid],['Signal',s.rssi+' dBm',s.rssi>-67?'ok':s.rssi>-75?'warn':'bad'],['IP',s.ip],['Modem sleep',s.wifiSleep?'on':'off']]);
 dl($('#sdisp'),[['Last update',s.epd.kind+' in '+s.epd.ms+' ms'],['Refresh wait',s.epd.refreshMs+' ms'],['BUSY',s.epd.busyTimeout?'TIMEOUT (panel not responding)':'ok',s.epd.busyTimeout?'bad':'ok'],['Partials since full',s.partials]]);
 dl($('#ssys'),[['Uptime',fmtS(s.uptime)],['Free heap',Math.round(s.heap/1024)+' KB'],['CPU',s.cpuMhz+' MHz'],['Firmware',s.fw]]);
 $('#strains').innerHTML=s.trains.length?'<table style="border-collapse:collapse"><tr><th align=left>Train</th><th align=left>Sched</th><th align=left>Est</th><th align=left>Status</th></tr>'+s.trains.map(t=>`<tr><td style="padding:2px 14px 2px 0">${t.number}</td><td style="padding:2px 14px 2px 0">${t.sched}</td><td style="padding:2px 14px 2px 0">${t.est}</td><td>${t.status}</td></tr>`).join('')+'</table>':'none';
 if(dev!=s.devMode){dev=s.devMode;$('#devtab').style.display=dev?'':'none'}
}catch(e){$('#hdr').textContent='board unreachable'}}
async function loadFb(){try{const r=await fetch('/fb.bin',{cache:'no-store'});const b=new Uint8Array(await r.arrayBuffer());const c=$('#cv'),x=c.getContext('2d'),im=x.createImageData(1360,480),d=im.data;
 for(let i=0,p=0;i<b.length;i++){const v=b[i];for(let k=7;k>=0;k--,p+=4){const w=(v>>k)&1?0:255;d[p]=d[p+1]=d[p+2]=w;d[p+3]=255}}x.putImageData(im,0,0);$('#fbinfo').textContent='updated '+new Date().toLocaleTimeString()}catch(e){$('#fbinfo').textContent='preview failed'}}
async function loadSettings(){const s=await (await fetch('/api/settings')).json();const f=$('#sform');for(const k in s){const el=f.elements[k];if(!el)continue;if(el.type=='checkbox')el.checked=!!s[k];else el.value=s[k]}}
async function saveSettings(ev){ev.preventDefault();const f=$('#sform'),o={};for(const el of f.elements){if(!el.name)continue;o[el.name]=el.type=='checkbox'?el.checked:(el.type=='number'?+el.value:el.value)}
 const r=await fetch('/api/settings',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(o)});toast(r.ok?'Settings saved':'Save failed');loadSettings();loadStatus();return false}
async function resetSettings(){if(!confirm('Reset all settings to defaults?'))return;await fetch('/api/settings/reset',{method:'POST'});toast('Defaults restored');loadSettings();loadStatus()}
async function act(a,conf){if(conf&&!confirm(conf))return;toast('Working...');const r=await fetch('/api/action?do='+a,{method:'POST'});toast(r.ok?await r.text():'Failed: '+await r.text());setTimeout(()=>{loadStatus();if($('#preview').classList.contains('on'))loadFb()},800)}
async function loadLog(){try{$('#log').textContent=await (await fetch('/api/log')).text();if($('#logauto').checked)$('#log').scrollTop=1e9}catch(e){}}
async function loadFeed(){try{$('#feed').textContent=JSON.stringify(await (await fetch('/api/feed')).json(),null,1)}catch(e){}}
async function simOn(){const rows=$('#sim').value.split('\n').map(l=>l.split(',').map(x=>x.trim())).filter(r=>r.length>=2).map(r=>({number:r[0],departure:r[1],delay:r[2]||'On time'}));
 const r=await fetch('/api/simulate',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(rows)});toast(r.ok?'Simulation applied':'Failed');setTimeout(loadStatus,800)}
async function simOff(){await fetch('/api/simulate',{method:'POST',headers:{'content-type':'application/json'},body:'[]'});toast('Back to live feed');setTimeout(loadStatus,800)}
loadStatus();loadSettings();setInterval(loadStatus,5000);
setInterval(()=>{if($('#preview').classList.contains('on')&&$('#auto').checked)loadFb();if($('#dev').classList.contains('on')&&$('#logauto').checked)loadLog()},10000);
</script></body></html>)HTML";

class WebUi {
 public:
  void begin(WebServer& srv, Settings* st, WebHooks hooks) {
    s_ = &srv; st_ = st; h_ = hooks;
    srv.on("/", HTTP_GET, [this]() { s_->send_P(200, "text/html", UI_HTML); });
    srv.on("/api/status", HTTP_GET, [this]() {
      JsonDocument d; JsonObject o = d.to<JsonObject>();
      if (h_.status) h_.status(o);
      o["devMode"] = st_->devMode; o["wifiSleep"] = st_->wifiSleep;
      sendJson(d);
    });
    srv.on("/api/settings", HTTP_GET, [this]() { JsonDocument d; st_->toJson(d.to<JsonObject>()); sendJson(d); });
    srv.on("/api/settings", HTTP_POST, [this]() {
      JsonDocument d;
      if (deserializeJson(d, s_->arg("plain"))) { s_->send(400, "text/plain", "bad json"); return; }
      st_->fromJson(d.as<JsonObjectConst>()); st_->save();
      if (h_.settingsChanged) h_.settingsChanged();
      LOG("[cfg] settings saved (poll %us, full every %u/%umin, sleep %d, cpu80 %d, dev %d)\n",
          st_->pollSec, st_->fullEveryN, st_->fullMaxAgeMin, st_->wifiSleep, st_->cpu80, st_->devMode);
      s_->send(200, "text/plain", "ok");
    });
    srv.on("/api/settings/reset", HTTP_POST, [this]() {
      Settings::reset(); *st_ = Settings(); if (h_.settingsChanged) h_.settingsChanged();
      LOG("[cfg] settings reset to defaults\n"); s_->send(200, "text/plain", "ok");
    });
    srv.on("/fb.bin", HTTP_GET, [this]() {
      if (!h_.shown) { s_->send(503, "text/plain", "no framebuffer"); return; }
      s_->sendHeader("Cache-Control", "no-store");
      s_->send_P(200, "application/octet-stream", (PGM_P)h_.shown->px, FB_BYTES);
    });
    srv.on("/api/feed", HTTP_GET, [this]() { JsonDocument d; if (h_.feed) h_.feed(d.to<JsonArray>()); sendJson(d); });
    srv.on("/api/log", HTTP_GET, [this]() {
      static char buf[LogRing::CAP + 1]; LOGR.read(buf, sizeof buf);
      s_->send(200, "text/plain", buf);
    });
    srv.on("/api/action", HTTP_POST, [this]() {
      String a = s_->arg("do");
      bool devOnly = a != "poll" && a != "resetwifi";
      if (devOnly && !st_->devMode) { s_->send(403, "text/plain", "developer mode is off"); return; }
      // Reply first for actions that reboot; the rest reply after running.
      if (a == "reboot" || a == "resetwifi") {
        s_->send(200, "text/plain", a == "reboot" ? "Rebooting" : "Restarting into setup mode"); delay(300);
      }
      bool ok = h_.action && h_.action(a.c_str());
      if (a != "reboot" && a != "resetwifi") s_->send(ok ? 200 : 400, "text/plain", ok ? ("Done: " + a) : ("Unknown action: " + a));
    });
    srv.on("/api/simulate", HTTP_POST, [this]() {
      if (!st_->devMode) { s_->send(403, "text/plain", "developer mode is off"); return; }
      JsonDocument d;
      if (deserializeJson(d, s_->arg("plain")) || !d.is<JsonArray>()) { s_->send(400, "text/plain", "bad json"); return; }
      bool ok = h_.simulate && h_.simulate(d.as<JsonArrayConst>());
      s_->send(ok ? 200 : 400, "text/plain", ok ? "ok" : "rejected");
    });
    srv.onNotFound([this]() { s_->send(404, "text/plain", "not found"); });
    srv.begin();
  }

 private:
  WebServer* s_ = nullptr; Settings* st_ = nullptr; WebHooks h_;
  void sendJson(JsonDocument& d) { String out; serializeJson(d, out); s_->send(200, "application/json", out); }
};

}  // namespace septa
