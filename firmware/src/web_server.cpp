#include "web_server.h"
#include "settings.h"
#include "adsb.h"
#include "net_task.h"
#include "notify.h"
#include "stats.h"
#include "extras.h"
#include "ntp.h"
#include "ui/ui_main.h"
#include "ui/ui_radar.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "net_util.h"
#include "airports.h"

static WebServer g_srv(80);
static bool      g_started = false;
static uint32_t  g_ota_until = 0;
static const char* HOSTNAME = "esp32-radar";

const char* web_server_hostname() { return HOSTNAME; }
bool web_server_started() { return g_started; }
void web_server_arm_ota(uint32_t seconds) { g_ota_until = millis() + seconds * 1000UL; }
bool web_server_ota_armed() { return g_ota_until && (int32_t)(g_ota_until - millis()) > 0; }

static bool auth() {
    const Settings& s = settings_get();
    if (!s.panel_pass[0]) return true;
    if (g_srv.authenticate("admin", s.panel_pass)) return true;
    g_srv.requestAuthentication(BASIC_AUTH, "esp32-radar");
    return false;
}

// ── embedded panel page ──────────────────────────────────────────────────────
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 ADS-B Radar</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"><script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script><style>
body{margin:0;background:#000;color:#33d650;font:14px/1.4 system-ui,sans-serif}a{color:#3df25a}
header{padding:10px 14px;border-bottom:1px solid #1fa84a;display:flex;gap:14px;align-items:center;flex-wrap:wrap}
header h1{font-size:18px;margin:0;color:#3df25a}nav button{background:#07200f;color:#3df25a;border:1px solid #1fa84a;border-radius:6px;padding:6px 12px;margin-right:6px}
nav button.on{background:#1fa84a;color:#000}section{padding:12px 14px;display:none}section.on{display:block}
table{border-collapse:collapse;width:100%}td,th{padding:4px 6px;border-bottom:1px solid #0e5a27;text-align:left;white-space:nowrap}th{color:#1e9a45;font-weight:normal}
tr.emg td{color:#ff3b3b}tr.watch td{color:#ffe44d}tr.mil td{color:#ffa030}
label{display:block;margin:8px 0 2px;color:#1e9a45}input,select{width:100%;max-width:420px;background:#04120a;color:#33d650;border:1px solid #1fa84a;border-radius:4px;padding:6px;box-sizing:border-box}
input[type=checkbox]{width:auto}.row{display:flex;gap:16px;flex-wrap:wrap}.row>div{flex:1;min-width:220px}
button.save{margin-top:14px;background:#1fa84a;color:#000;border:0;border-radius:6px;padding:10px 18px;font-weight:bold}
.muted{color:#1e9a45}.kv span{display:inline-block;min-width:160px;color:#1e9a45}pre{white-space:pre-wrap;color:#33d650}
img.shot{border:1px solid #1fa84a;image-rendering:pixelated;width:272px;height:480px}
#map{height:calc(100vh - 70px);min-height:420px;background:#000}.leaflet-tile{filter:invert(1) hue-rotate(90deg) saturate(.4) brightness(.75)}
.ac{color:#8cff7a;font-size:18px;line-height:18px;text-shadow:0 0 3px #000}.ac.sel{color:#ffe44d}.ac.watch{color:#ffe44d}.ac.mil{color:#ffa030}.ac.emg{color:#ff3b3b}
.aclbl{color:#33d650;font:11px system-ui;white-space:nowrap;text-shadow:0 0 3px #000;margin-left:14px}.apt{color:#1e9a45;font:11px system-ui;white-space:nowrap}
.leaflet-popup-content-wrapper,.leaflet-popup-tip{background:#04120a;color:#33d650;border:1px solid #1fa84a}
</style></head><body>
<header><h1>ESP32 ADS-B Radar</h1><nav><button data-s="live" class="on">Live</button><button data-s="map">Map</button><button data-s="airports">Airports</button><button data-s="alerts">Alerts</button><button data-s="stats">Stats</button><button data-s="settings">Settings</button><button data-s="api">API</button></nav><span id="hdr" class="muted"></span></header>
<section id="live" class="on"><div class="row"><div><div class="kv" id="summary"></div><table id="tbl"><thead><tr><th>Callsign</th><th>Type</th><th>Reg</th><th>Airline</th><th>Route</th><th>Alt ft</th><th>Spd kt</th><th>Trk</th><th>Dist km</th><th>Brg</th><th>Sqk</th><th>Class</th></tr></thead><tbody></tbody></table></div>
<div style="flex:0 0 290px"><img class="shot" id="shot" alt="screen"><br><button onclick="document.getElementById('shot').src='/screen.bmp?'+Date.now()">Refresh screenshot</button></div></div></section>
<section id="map" style="padding:0"><div id="map"></div></section>
<section id="airports"><label>Airport</label><select id="aptsel" onchange="airports()"></select><div class="row"><div><h3>Departures</h3><table id="dep"><thead><tr><th>Flight</th><th>Airline</th><th>Type</th><th>To</th><th>Alt ft</th><th>Out km</th></tr></thead><tbody></tbody></table></div>
<div><h3>Arrivals</h3><table id="arr"><thead><tr><th>Flight</th><th>Airline</th><th>Type</th><th>From</th><th>Alt ft</th><th>Dist km</th><th>ETA</th></tr></thead><tbody></tbody></table></div></div><p class="muted">Built from the aircraft currently tracked and their route database entries; ETA = distance to the airport at current ground speed.</p></section>
<section id="alerts"><table id="atbl"><thead><tr><th>Time</th><th>Kind</th><th>Title</th><th>Message</th></tr></thead><tbody></tbody></table></section>
<section id="stats"><div id="stbox"></div></section>
<section id="settings"><form id="f" onsubmit="return save(event)"><div class="row">
<div><h3>Location</h3><label>Mode</label><select name="loc_mode"><option value="0">Auto (public IP)</option><option value="1">City</option><option value="2">Coordinates</option></select>
<label>City</label><input name="city"><label>Country code (ISO-2)</label><input name="country_cc" maxlength="2"><label>Latitude</label><input name="lat" type="number" step="0.0001"><label>Longitude</label><input name="lon" type="number" step="0.0001">
<h3>Radar</h3><label>Range km</label><select name="range_km"><option>10</option><option>25</option><option>50</option><option>75</option><option>100</option><option>150</option><option>200</option><option>300</option><option>400</option></select>
<label>Update every (s)</label><select name="update_s"><option>2</option><option>3</option><option>5</option><option>10</option><option>15</option><option>30</option><option>60</option></select>
<label>Units</label><select name="units"><option value="0">Metric</option><option value="1">Aviation</option></select>
<label>Data source</label><select name="source"><option value="0">Auto failover</option><option value="1">adsb.lol</option><option value="2">adsb.fi</option></select>
<label>Local receiver URL (dump1090 / readsb aircraft.json)</label><input name="local_url" placeholder="http://192.168.1.50:8080/data/aircraft.json">
<label>Overhead alert radius km (0 = off)</label><input name="alert_km" type="number" min="0" max="10">
<label>Brightness 10-255</label><input name="brightness" type="number" min="10" max="255">
<label><input type="checkbox" name="sweep"> Sweep</label><label><input type="checkbox" name="trails"> Trails</label><label><input type="checkbox" name="labels"> Labels</label><label><input type="checkbox" name="hide_ground"> Hide ground</label><label><input type="checkbox" name="auto_range"> Auto range</label><label><input type="checkbox" name="night_dim"> Dim at night</label><label><input type="checkbox" name="alert_bright"> Full brightness on alert</label><label><input type="checkbox" name="map_underlay"> Map underlay</label><label><input type="checkbox" name="airports"> Airport markers</label><label><input type="checkbox" name="iss"> Show the ISS</label><label><input type="checkbox" name="metar"> METAR</label>
<h3>Classes shown</h3><label><input type="checkbox" name="cls0"> Airliners</label><label><input type="checkbox" name="cls1"> Light aircraft</label><label><input type="checkbox" name="cls2"> Helicopters</label><label><input type="checkbox" name="cls3"> Military</label><label><input type="checkbox" name="cls4"> Other</label></div>
<div><h3>Wi-Fi</h3><label>SSID</label><input name="wifi_ssid" list="ssids"><datalist id="ssids"></datalist><button type="button" onclick="scan()">Scan networks</button><label>Password (leave blank to keep)</label><input name="wifi_password" type="password">
<h3>Integrations</h3><label>Watchlist (comma-separated registration / callsign / type prefixes)</label><input name="watchlist" placeholder="LN-,SAS,A388">
<label>ntfy.sh topic</label><input name="ntfy_topic"><label>Webhook URL</label><input name="webhook_url"><label>MQTT broker URI</label><input name="mqtt_uri" placeholder="mqtt://user:pass@192.168.1.50:1883">
<label><input type="checkbox" name="notify_emergency"> Push emergencies</label><label><input type="checkbox" name="notify_watch"> Push watchlist / notable</label><label><input type="checkbox" name="notify_alert"> Push overhead alerts</label>
<h3>Panel</h3><label>Panel password (user admin, blank = open)</label><input name="panel_pass" type="password">
<h3>Firmware update</h3><p class="muted" id="otastate"></p><input type="file" id="fw" accept=".bin"><button type="button" onclick="ota()">Upload firmware</button><p class="muted">OTA must be armed from the device (Info page) first. Use firmware.bin, not the merged installer image.</p>
</div></div><button class="save">Save settings</button> <span id="msg" class="muted"></span></form></section>
<section id="api"><pre>GET  /api/state      live JSON: aircraft, stats, weather, location, ISS, METAR
GET  /api/config     settings (passwords omitted)
POST /api/config     JSON subset of settings; saves, applies, re-locates when needed
GET  /api/alerts     alert log JSON
GET  /api/stats      session / daily statistics JSON
GET  /api/wifi/scan  nearby networks JSON
GET  /screen.bmp     live screenshot (272x480)
GET  /metrics        Prometheus metrics
POST /ota            firmware.bin upload (403 unless armed on the device)
POST /api/action     {"action":"locate"|"reboot"|"reset_stats"|"page","page":0-4}</pre></section>
<script>
const $=s=>document.querySelector(s);window.onerror=(m,src,l,c)=>{$('#hdr').textContent='JS error: '+m+' @'+l+':'+c;};document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.remove('on'));document.querySelectorAll('section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.s).classList.add('on');if(b.dataset.s=='alerts')alerts();if(b.dataset.s=='stats')stats();if(b.dataset.s=='settings')cfg();if(b.dataset.s=='map')initMap();if(b.dataset.s=='airports')airports();});
let S=null,SEL=null,MAP=null,LAYER=null,APTS=[];
async function live(){try{const d=await (await fetch('/api/state')).json();S=d;if(MAP)drawMap();if($('#airports').classList.contains('on'))airports();$('#hdr').textContent=d.location.place+' · '+d.stats.count+' aircraft · '+d.stats.source+' · '+(d.weather.valid?d.weather.temp+'°C '+d.weather.cond:'');
if(!$('#shot').src)$('#shot').src='/screen.bmp';$('#summary').innerHTML=`<div><span>Location</span>${d.location.place} (${d.location.lat.toFixed(4)}, ${d.location.lon.toFixed(4)})</div><div><span>Wind</span>${d.weather.valid?d.weather.wind+' m/s '+d.weather.wind_dir+' gust '+d.weather.gust:'-'}</div><div><span>Last poll</span>${d.stats.age_s}s ago, ${d.stats.latency_ms} ms</div><div><span>ISS</span>${d.iss.valid?d.iss.dist_km.toFixed(0)+' km '+d.iss.bearing.toFixed(0)+'°':'-'}</div><div><span>METAR</span>${d.metar.valid?d.metar.raw:'-'}</div>`;
const tb=$('#tbl tbody');tb.innerHTML='';d.aircraft.forEach(a=>{const tr=document.createElement('tr');tr.className=a.emergency?'emg':(a.watch||a.notable)?'watch':a.military?'mil':'';tr.onclick=()=>{SEL=a.hex;document.querySelector('nav button[data-s=map]').click();};tr.innerHTML=`<td>${a.flight||a.hex}</td><td>${a.type}</td><td>${a.reg}</td><td>${a.airline}</td><td>${a.route} ${a.dest_city}</td><td>${a.on_ground?'ground':a.alt_ft}</td><td>${a.gs_kt}</td><td>${a.track}</td><td>${a.dist_km.toFixed(1)}</td><td>${a.bearing.toFixed(0)}</td><td>${a.squawk}</td><td>${a.cls}</td>`;tb.appendChild(tr);});}catch(e){$('#hdr').textContent='live() error: '+e;}}
async function alerts(){const d=await (await fetch('/api/alerts')).json();const tb=$('#atbl tbody');tb.innerHTML='';d.forEach(a=>{const tr=document.createElement('tr');tr.innerHTML=`<td>${a.time}</td><td>${a.kind}</td><td>${a.title}</td><td>${a.message}</td>`;tb.appendChild(tr);});}
async function stats(){const d=await (await fetch('/api/stats')).json();let h=`<div class="kv"><div><span>Unique today</span>${d.unique_today}</div><div><span>Max tracked</span>${d.max_tracked}</div><div><span>Highest</span>${d.max_alt_who} ${d.max_alt_ft} ft</div><div><span>Fastest</span>${d.max_gs_who} ${d.max_gs_kt} kt</div><div><span>Closest</span>${d.min_dist_who} ${d.min_dist_km.toFixed(1)} km</div><div><span>Emergencies / alerts</span>${d.emergencies} / ${d.alerts}</div><div><span>Boots</span>${d.boot_count}</div></div><h3>Per hour</h3><pre>`;d.hourly.forEach((v,i)=>{h+=String(i).padStart(2,'0')+' '+'#'.repeat(Math.min(v,60))+' '+v+'\n'});h+='</pre><h3>Top airlines</h3><pre>'+d.airlines.map(a=>a.key+' '+a.count).join('\n')+'</pre><h3>Top types</h3><pre>'+d.types.map(a=>a.key+' '+a.count).join('\n')+'</pre>';$('#stbox').innerHTML=h;}
async function cfg(){const d=await (await fetch('/api/config')).json();const f=$('#f');for(const k in d){const el=f.elements[k];if(!el||el.type=='file'||el.tagName=='BUTTON')continue;if(el.type=='checkbox')el.checked=!!d[k];else el.value=d[k];}for(let i=0;i<5;i++)f.elements['cls'+i].checked=!!(d.class_mask&(1<<i));$('#otastate').textContent=d.ota_armed?'OTA armed':'OTA locked (arm it on the device: Info page)';}
async function save(e){e.preventDefault();const f=$('#f');const o={};for(const el of f.elements){if(!el.name)continue;if(el.name.startsWith('cls'))continue;if(el.type=='checkbox')o[el.name]=el.checked;else if(el.value!=='')o[el.name]=isNaN(el.value)||el.name=='city'||el.name=='country_cc'||el.name.includes('url')||el.name.includes('_')&&typeof el.value=='string'&&!/^-?\d+(\.\d+)?$/.test(el.value)?el.value:Number(el.value);}
let m=0;for(let i=0;i<5;i++)if(f.elements['cls'+i].checked)m|=1<<i;o.class_mask=m;const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});$('#msg').textContent=await r.text();}
async function scan(){$('#msg').textContent='scanning...';const d=await (await fetch('/api/wifi/scan')).json();const dl=$('#ssids');dl.innerHTML='';d.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' dBm';dl.appendChild(o);});$('#msg').textContent=d.length+' networks found - pick one in the SSID field';}
async function ota(){const fl=$('#fw').files[0];if(!fl){alert('choose firmware.bin');return;}const fd=new FormData();fd.append('firmware',fl);$('#msg').textContent='uploading '+fl.size+' bytes...';const r=await fetch('/ota',{method:'POST',body:fd});$('#msg').textContent=await r.text();}

function gc(a,b,n){const r=Math.PI/180,p1=a[0]*r,l1=a[1]*r,p2=b[0]*r,l2=b[1]*r;const d=2*Math.asin(Math.sqrt(Math.sin((p2-p1)/2)**2+Math.cos(p1)*Math.cos(p2)*Math.sin((l2-l1)/2)**2));if(d<1e-6)return[a,b];const o=[];for(let i=0;i<=n;i++){const f=i/n,A=Math.sin((1-f)*d)/Math.sin(d),B=Math.sin(f*d)/Math.sin(d);const x=A*Math.cos(p1)*Math.cos(l1)+B*Math.cos(p2)*Math.cos(l2),y=A*Math.cos(p1)*Math.sin(l1)+B*Math.cos(p2)*Math.sin(l2),z=A*Math.sin(p1)+B*Math.sin(p2);o.push([Math.atan2(z,Math.sqrt(x*x+y*y))/r,Math.atan2(y,x)/r]);}return o;}
function km(a,b){const r=Math.PI/180,dl=(b[0]-a[0])*r,dn=(b[1]-a[1])*r,h=Math.sin(dl/2)**2+Math.cos(a[0]*r)*Math.cos(b[0]*r)*Math.sin(dn/2)**2;return 6371*2*Math.atan2(Math.sqrt(h),Math.sqrt(1-h));}
async function initMap(){if(MAP){setTimeout(()=>MAP.invalidateSize(),50);drawMap();return;}if(!window.L){$('#map').textContent='Leaflet failed to load (no internet?)';return;}
const c=S?[S.location.lat,S.location.lon]:[60,10];MAP=L.map('map',{zoomControl:true}).setView(c,8);L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:18,attribution:'&copy; OpenStreetMap contributors'}).addTo(MAP);
LAYER=L.layerGroup().addTo(MAP);if(!APTS.length)APTS=await (await fetch('/api/airports')).json();APTS.forEach(p=>L.marker([p.lat,p.lon],{icon:L.divIcon({className:'apt',html:'&#10010; '+(p.iata||p.icao)})}).addTo(MAP));drawMap();}
function drawMap(){if(!MAP||!S)return;LAYER.clearLayers();const c=[S.location.lat,S.location.lon];L.circleMarker(c,{radius:5,color:'#3df25a'}).addTo(LAYER);[S.stats.range_km||100].forEach(r=>L.circle(c,{radius:r*1000,color:'#1fa84a',weight:1,fill:false}).addTo(LAYER));
S.aircraft.forEach(a=>{const sel=a.hex===SEL;const cls=sel?'sel':a.emergency?'emg':(a.watch||a.notable)?'watch':a.military?'mil':'';const ic=L.divIcon({className:'',html:`<span class="ac ${cls}" style="display:inline-block;transform:rotate(${a.track}deg)">&#9650;</span><span class="aclbl">${a.flight||a.hex}</span>`,iconSize:[18,18],iconAnchor:[9,9]});
const m=L.marker([a.lat,a.lon],{icon:ic}).addTo(LAYER);m.bindPopup(`<b>${a.flight||a.hex}</b> ${a.airline}<br>${a.type} ${a.reg}<br>${a.route} ${a.org_city?a.org_city+' → '+a.dest_city:''}<br>${a.alt_ft} ft, ${a.gs_kt} kt, trk ${a.track}°<br>${a.dist_km.toFixed(1)} km, brg ${a.bearing.toFixed(0)}°`);m.on('click',()=>{SEL=a.hex;drawMap();});
if(a.trail&&a.trail.length)L.polyline([...a.trail,[a.lat,a.lon]],{color:sel?'#ffe44d':'#1fa84a',weight:sel?2:1,opacity:.8}).addTo(LAYER);
if(sel){if(a.org_lat)L.polyline(gc([a.org_lat,a.org_lon],[a.lat,a.lon],48),{color:'#ffe44d',weight:2,opacity:.9}).addTo(LAYER);if(a.dst_lat)L.polyline(gc([a.lat,a.lon],[a.dst_lat,a.dst_lon],48),{color:'#ffe44d',weight:2,dashArray:'6 6',opacity:.9}).addTo(LAYER);
if(a.org_lat)L.marker([a.org_lat,a.org_lon],{icon:L.divIcon({className:'apt',html:'&#9873; '+a.route.split('-')[0]+' '+a.org_city})}).addTo(LAYER);if(a.dst_lat)L.marker([a.dst_lat,a.dst_lon],{icon:L.divIcon({className:'apt',html:'&#9873; '+a.route.split('-').pop()+' '+a.dest_city})}).addTo(LAYER);}});}
async function airports(){if(!S)return;if(!APTS.length)APTS=await (await fetch('/api/airports')).json();const sel=$('#aptsel');const codes=new Map();APTS.forEach(p=>{if(p.iata)codes.set(p.iata,p.name)});S.aircraft.forEach(a=>{if(!a.route)return;const r=a.route.split('-');if(!codes.has(r[0]))codes.set(r[0],a.org_city||'');const d=r[r.length-1];if(!codes.has(d))codes.set(d,a.dest_city||'');});
const cur=sel.value||'OSL';if(sel.options.length!==codes.size){sel.innerHTML='';[...codes.keys()].sort().forEach(k=>{const o=document.createElement('option');o.value=k;o.textContent=k+'  '+(codes.get(k)||'');sel.appendChild(o);});sel.value=codes.has(cur)?cur:[...codes.keys()][0];}
const ap=sel.value;const pos=APTS.find(p=>p.iata===ap);const dep=$('#dep tbody'),arr=$('#arr tbody');dep.innerHTML='';arr.innerHTML='';
S.aircraft.forEach(a=>{if(!a.route)return;const r=a.route.split('-'),org=r[0],dst=r[r.length-1];if(org===ap){const out=pos?km([a.lat,a.lon],[pos.lat,pos.lon]):(a.org_lat?km([a.lat,a.lon],[a.org_lat,a.org_lon]):NaN);const tr=document.createElement('tr');tr.onclick=()=>{SEL=a.hex;document.querySelector('nav button[data-s=map]').click();};tr.innerHTML=`<td>${a.flight||a.hex}</td><td>${a.airline}</td><td>${a.type}</td><td>${dst} ${a.dest_city}</td><td>${a.on_ground?'ground':a.alt_ft}</td><td>${isNaN(out)?'':out.toFixed(0)}</td>`;dep.appendChild(tr);}
if(dst===ap){const p=pos?[pos.lat,pos.lon]:(a.dst_lat?[a.dst_lat,a.dst_lon]:null);const d=p?km([a.lat,a.lon],p):NaN;const eta=(!isNaN(d)&&a.gs_kt>60)?Math.round(d/(a.gs_kt*1.852)*60):null;const tr=document.createElement('tr');tr.onclick=()=>{SEL=a.hex;document.querySelector('nav button[data-s=map]').click();};tr.innerHTML=`<td>${a.flight||a.hex}</td><td>${a.airline}</td><td>${a.type}</td><td>${org} ${a.org_city}</td><td>${a.on_ground?'ground':a.alt_ft}</td><td>${isNaN(d)?'':d.toFixed(0)}</td><td>${eta===null?(a.on_ground?'landed':''):'~'+eta+' min'}</td>`;arr.appendChild(tr);}});
if(!dep.children.length)dep.innerHTML='<tr><td colspan=6 class="muted">none in range right now</td></tr>';if(!arr.children.length)arr.innerHTML='<tr><td colspan=7 class="muted">none in range right now</td></tr>';}
live();setInterval(live,5000);if(location.hash){const b=document.querySelector('nav button[data-s='+location.hash.slice(1)+']');if(b)setTimeout(()=>b.click(),300);}
</script></body></html>)HTML";

// ── JSON helpers ─────────────────────────────────────────────────────────────
// Serialize into PSRAM (a String would not fit internal RAM for big states), then send in one piece.
static PsramBuffer g_json;
static void send_json(JsonDocument& doc) {
    g_json.clear();
    serializeJson(doc, g_json);
    g_srv.sendHeader("Access-Control-Allow-Origin", "*");
    g_srv.setContentLength(g_json.size());
    g_srv.send(200, "application/json", "");
    g_srv.sendContent(g_json.data(), g_json.size());
}

static void handle_state() {
    if (!auth()) return;
    const Settings& s = settings_get();
    JsonDocument doc(&g_psram_alloc);
    JsonObject loc = doc["location"].to<JsonObject>();
    loc["place"] = s.place; loc["lat"] = s.lat; loc["lon"] = s.lon; loc["mode"] = s.loc_mode;
    const LocalInfo& li = net_local_info();
    JsonObject wx = doc["weather"].to<JsonObject>();
    wx["valid"] = li.valid; wx["temp"] = li.temp_c; wx["cond"] = geo_wmo_short(li.wmo_code); wx["wind"] = li.wind_mps; wx["gust"] = li.gust_mps; wx["wind_dir"] = adsb_compass16(li.wind_dir); wx["tz"] = li.tz_name;
    const IssState& iss = extras_iss();
    JsonObject jiss = doc["iss"].to<JsonObject>(); jiss["valid"] = iss.valid; jiss["lat"] = iss.lat; jiss["lon"] = iss.lon; jiss["alt_km"] = iss.alt_km; jiss["dist_km"] = iss.dist_km; jiss["bearing"] = iss.bearing;
    const MetarState& me = extras_metar();
    JsonObject jm = doc["metar"].to<JsonObject>(); jm["valid"] = me.valid; jm["station"] = me.station; jm["raw"] = me.raw;
    adsb_lock();
    const AdsbStats& st = adsb_stats();
    JsonObject js = doc["stats"].to<JsonObject>();
    js["count"] = st.count; js["emergencies"] = st.emergencies; js["alerts"] = st.alerts; js["watch"] = st.watch_count; js["source"] = st.source;
    js["age_s"] = st.last_ok_ms ? (millis() - st.last_ok_ms) / 1000 : -1; js["latency_ms"] = st.last_latency_ms; js["fetches"] = st.total_fetches; js["failures"] = st.total_failures;
    js["heap_kb"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024; js["uptime_s"] = millis() / 1000; js["fw"] = FW_VERSION; js["range_km"] = s.range_km;
    JsonArray arr = doc["aircraft"].to<JsonArray>();
    Aircraft* ac = adsb_list(); int n = adsb_count();
    for (int i = 0; i < n; i++) {
        const Aircraft& a = ac[i];
        if (!a.hex[0] || !adsb_visible(a)) continue;
        JsonObject o = arr.add<JsonObject>();
        o["hex"] = a.hex; o["flight"] = a.flight; o["reg"] = a.reg; o["type"] = a.type; o["airline"] = adsb_airline_name(a.flight);
        o["lat"] = a.lat; o["lon"] = a.lon; o["alt_ft"] = a.alt_ft; o["on_ground"] = a.on_ground; o["gs_kt"] = a.gs_kt; o["track"] = a.track; o["vr_fpm"] = a.vr_fpm;
        o["squawk"] = a.squawk; o["emergency"] = a.emergency; o["military"] = a.military; o["category"] = a.category; o["cls"] = adsb_class_name(a.cls);
        o["dist_km"] = a.dist_km; o["bearing"] = a.bearing; o["elev"] = a.elev_deg; o["cpa_km"] = a.cpa_km; o["cpa_min"] = a.cpa_min; o["alert"] = a.alert; o["watch"] = a.watch; o["notable"] = a.notable;
        o["route"] = a.route; o["dest_city"] = a.dest_city; o["org_city"] = a.org_city; o["org_name"] = a.org_name; o["dest_name"] = a.dest_name;
        if (a.org_lat != 0) { o["org_lat"] = a.org_lat; o["org_lon"] = a.org_lon; }
        if (a.dst_lat != 0) { o["dst_lat"] = a.dst_lat; o["dst_lon"] = a.dst_lon; }
        JsonArray tr = o["trail"].to<JsonArray>();
        for (int j = 0; j < a.trail_n; j++) { int idx = (a.trail_head - a.trail_n + j + TRAIL_LEN) % TRAIL_LEN; JsonArray p = tr.add<JsonArray>(); p.add(a.trail_lat[idx]); p.add(a.trail_lon[idx]); }
    }
    adsb_unlock();
    send_json(doc);
}

static void config_to_json(JsonDocument& doc) {
    const Settings& s = settings_get();
    doc["wifi_ssid"] = s.wifi_ssid; doc["loc_mode"] = s.loc_mode; doc["city"] = s.city; doc["country_cc"] = s.country_cc; doc["place"] = s.place;
    doc["lat"] = s.lat; doc["lon"] = s.lon; doc["range_km"] = s.range_km; doc["update_s"] = s.update_s; doc["units"] = s.units; doc["source"] = s.source;
    doc["sweep"] = s.sweep; doc["trails"] = s.trails; doc["labels"] = s.labels; doc["hide_ground"] = s.hide_ground; doc["auto_range"] = s.auto_range; doc["night_dim"] = s.night_dim;
    doc["highlight_mil"] = s.highlight_mil; doc["brightness"] = s.brightness; doc["alert_km"] = s.alert_km; doc["alert_bright"] = s.alert_bright; doc["map_underlay"] = s.map_underlay; doc["airports"] = s.airports;
    doc["watchlist"] = s.watchlist; doc["ntfy_topic"] = s.ntfy_topic; doc["webhook_url"] = s.webhook_url; doc["mqtt_uri"] = s.mqtt_uri[0] ? "(set)" : ""; doc["local_url"] = s.local_url;
    doc["class_mask"] = s.class_mask; doc["iss"] = s.iss; doc["metar"] = s.metar; doc["notify_emergency"] = s.notify_emergency; doc["notify_watch"] = s.notify_watch; doc["notify_alert"] = s.notify_alert;
    doc["panel_pass_set"] = s.panel_pass[0] != 0; doc["ota_armed"] = web_server_ota_armed(); doc["fw"] = FW_VERSION; doc["hostname"] = HOSTNAME;
}
static void handle_config_get() { if (!auth()) return; JsonDocument doc(&g_psram_alloc); config_to_json(doc); send_json(doc); }

static void handle_config_post() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    if (deserializeJson(doc, g_srv.arg("plain"))) { g_srv.send(400, "text/plain", "bad json"); return; }
    Settings& s = settings_get();
    bool relocate = false, reconnect = false, redraw = false;
    auto str = [&](const char* k, char* dst, size_t n) { if (doc[k].is<const char*>()) strlcpy(dst, doc[k] | "", n); };
    if (doc["wifi_ssid"].is<const char*>() && strcmp(s.wifi_ssid, doc["wifi_ssid"] | "")) { str("wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid)); reconnect = true; }
    if (doc["wifi_password"].is<const char*>() && strlen(doc["wifi_password"] | "")) { str("wifi_password", s.wifi_password, sizeof(s.wifi_password)); reconnect = true; }
    if (doc["loc_mode"].is<int>()) { uint8_t m = doc["loc_mode"]; if (m != s.loc_mode) { s.loc_mode = m; relocate = true; } }
    if (doc["city"].is<const char*>() && strcmp(s.city, doc["city"] | "")) { str("city", s.city, sizeof(s.city)); relocate = true; }
    if (doc["country_cc"].is<const char*>()) str("country_cc", s.country_cc, sizeof(s.country_cc));
    if (doc["lat"].is<float>() && doc["lon"].is<float>() && s.loc_mode == LOC_MANUAL) { float la = doc["lat"], lo = doc["lon"]; if (fabsf(la - s.man_lat) > 1e-5f || fabsf(lo - s.man_lon) > 1e-5f) { s.man_lat = la; s.man_lon = lo; relocate = true; } }
    if (doc["range_km"].is<int>())  { s.range_km = doc["range_km"]; redraw = true; }
    if (doc["update_s"].is<int>())  s.update_s = max(2, (int)doc["update_s"]);
    if (doc["units"].is<int>())     { s.units = doc["units"]; redraw = true; }
    if (doc["source"].is<int>())    s.source = doc["source"];
    if (doc["alert_km"].is<int>())  s.alert_km = doc["alert_km"];
    if (doc["brightness"].is<int>()) s.brightness = max(10, min(255, (int)doc["brightness"]));
    if (doc["class_mask"].is<int>()) { s.class_mask = doc["class_mask"]; redraw = true; }
    auto bl = [&](const char* k, bool& dst) { if (doc[k].is<bool>()) dst = doc[k]; };
    bl("sweep", s.sweep); bl("trails", s.trails); bl("labels", s.labels); bl("hide_ground", s.hide_ground); bl("auto_range", s.auto_range); bl("night_dim", s.night_dim);
    bl("highlight_mil", s.highlight_mil); bl("alert_bright", s.alert_bright); bl("map_underlay", s.map_underlay); bl("airports", s.airports); bl("iss", s.iss); bl("metar", s.metar);
    bl("notify_emergency", s.notify_emergency); bl("notify_watch", s.notify_watch); bl("notify_alert", s.notify_alert);
    str("watchlist", s.watchlist, sizeof(s.watchlist)); str("ntfy_topic", s.ntfy_topic, sizeof(s.ntfy_topic)); str("webhook_url", s.webhook_url, sizeof(s.webhook_url));
    if (doc["mqtt_uri"].is<const char*>() && strcmp(doc["mqtt_uri"] | "", "(set)")) str("mqtt_uri", s.mqtt_uri, sizeof(s.mqtt_uri));
    str("local_url", s.local_url, sizeof(s.local_url));
    if (doc["panel_pass"].is<const char*>() && strlen(doc["panel_pass"] | "")) str("panel_pass", s.panel_pass, sizeof(s.panel_pass));
    settings_save();
    if (redraw) ui_radar_range_changed();
    if (reconnect) net_send(NC_CONNECT_WIFI);
    else if (relocate) {
        if (s.loc_mode == LOC_AUTO_IP) net_send(NC_LOCATE_IP);
        else if (s.loc_mode == LOC_CITY) net_send(NC_GEOCODE, s.city, s.country_cc);
        else net_send(NC_APPLY_MANUAL, nullptr, nullptr, s.man_lat, s.man_lon);
    }
    ui_main_request_fetch();
    g_srv.send(200, "text/plain", reconnect ? "saved - reconnecting WiFi" : relocate ? "saved - updating location" : "saved");
}

static void handle_alerts() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < notify_log_count(); i++) {
        const AlertEntry& e = notify_log_get(i);
        JsonObject o = arr.add<JsonObject>();
        char tbuf[24]; time_t t = e.epoch; struct tm tm; localtime_r(&t, &tm);
        if (e.epoch > 1600000000UL) strftime(tbuf, sizeof(tbuf), "%d.%m %H:%M", &tm); else snprintf(tbuf, sizeof(tbuf), "+%lus", (unsigned long)(e.ms / 1000));
        o["time"] = tbuf; o["kind"] = notify_kind_name((NotifyKind)e.kind); o["title"] = e.title; o["message"] = e.message;
    }
    send_json(doc);
}

static void handle_stats() {
    if (!auth()) return;
    const Stats& st = stats_get();
    JsonDocument doc(&g_psram_alloc);
    doc["unique_today"] = st.unique_today; doc["max_tracked"] = st.max_tracked; doc["max_alt_ft"] = st.max_alt_ft; doc["max_alt_who"] = st.max_alt_who;
    doc["max_gs_kt"] = st.max_gs_kt; doc["max_gs_who"] = st.max_gs_who; doc["min_dist_km"] = st.min_dist_km; doc["min_dist_who"] = st.min_dist_who;
    doc["emergencies"] = st.emergencies; doc["alerts"] = st.alerts; doc["boot_count"] = st.boot_count;
    JsonArray h = doc["hourly"].to<JsonArray>(); for (int i = 0; i < 24; i++) h.add(st.hourly[i]);
    JsonArray al = doc["airlines"].to<JsonArray>(); for (int i = 0; i < STATS_TOP; i++) if (st.airlines[i].key[0]) { JsonObject o = al.add<JsonObject>(); o["key"] = st.airlines[i].key; o["name"] = adsb_airline_name(st.airlines[i].key); o["count"] = st.airlines[i].count; }
    JsonArray ty = doc["types"].to<JsonArray>();    for (int i = 0; i < STATS_TOP; i++) if (st.types[i].key[0])    { JsonObject o = ty.add<JsonObject>(); o["key"] = st.types[i].key; o["name"] = adsb_type_name(st.types[i].key); o["count"] = st.types[i].count; }
    send_json(doc);
}

static void handle_airports() {
    if (!auth()) return;
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < AIRPORT_COUNT; i++) { JsonObject o = arr.add<JsonObject>(); o["icao"] = AIRPORTS[i].icao; o["iata"] = AIRPORTS[i].iata; o["name"] = AIRPORTS[i].name; o["lat"] = AIRPORTS[i].lat; o["lon"] = AIRPORTS[i].lon; }
    send_json(doc);
}

static void handle_scan() {
    if (!auth()) return;
    int n = WiFi.scanNetworks(false, false, false, 300);
    JsonDocument doc(&g_psram_alloc);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < n && i < 30; i++) { JsonObject o = arr.add<JsonObject>(); o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN; }
    WiFi.scanDelete();
    send_json(doc);
}

static void handle_screen() {
    if (!auth()) return;
    lv_img_dsc_t* snap = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (!snap) { g_srv.send(500, "text/plain", "snapshot failed"); return; }
    int w = snap->header.w, h = snap->header.h;
    uint32_t row = (w * 3 + 3) & ~3, size = 54 + row * h;
    uint8_t hdr[54] = {'B','M'};
    auto put32 = [&](int o, uint32_t v) { hdr[o] = v; hdr[o+1] = v >> 8; hdr[o+2] = v >> 16; hdr[o+3] = v >> 24; };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, (uint32_t)(-h)); hdr[26] = 1; hdr[28] = 24; put32(34, row * h);
    g_srv.setContentLength(size);
    g_srv.sendHeader("Access-Control-Allow-Origin", "*");
    g_srv.send(200, "image/bmp", "");
    g_srv.sendContent((const char*)hdr, 54);
    static uint8_t line[272 * 3 + 4];
    const uint16_t* px = (const uint16_t*)snap->data;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t v = px[y * w + x];
            line[x * 3 + 0] = (v & 31) * 255 / 31; line[x * 3 + 1] = ((v >> 5) & 63) * 255 / 63; line[x * 3 + 2] = (v >> 11) * 255 / 31;
        }
        g_srv.sendContent((const char*)line, row);
    }
    lv_snapshot_free(snap);
}

static void handle_metrics() {
    if (!auth()) return;
    adsb_lock(); AdsbStats st = adsb_stats(); adsb_unlock();
    const Stats& d = stats_get();
    char buf[700];
    snprintf(buf, sizeof(buf),
        "esp32radar_aircraft %d\nesp32radar_emergencies %d\nesp32radar_alerts %d\nesp32radar_unique_today %lu\nesp32radar_max_tracked %u\n"
        "esp32radar_fetches_total %lu\nesp32radar_failures_total %lu\nesp32radar_last_latency_ms %lu\nesp32radar_heap_free_bytes %u\nesp32radar_uptime_seconds %lu\nesp32radar_wifi_rssi %d\n",
        st.count, st.emergencies, st.alerts, (unsigned long)d.unique_today, d.max_tracked, (unsigned long)st.total_fetches, (unsigned long)st.total_failures,
        (unsigned long)st.last_latency_ms, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned long)(millis() / 1000), WiFi.RSSI());
    g_srv.send(200, "text/plain; version=0.0.4", buf);
}

static void handle_action() {
    if (!auth()) return;
    JsonDocument doc; deserializeJson(doc, g_srv.arg("plain"));
    const char* a = doc["action"] | "";
    if (!strcmp(a, "locate")) { net_send(NC_LOCATE_IP); g_srv.send(200, "text/plain", "locating"); }
    else if (!strcmp(a, "reboot")) { g_srv.send(200, "text/plain", "rebooting"); delay(200); ESP.restart(); }
    else if (!strcmp(a, "reset_stats")) { stats_reset(); g_srv.send(200, "text/plain", "stats reset"); }
    else if (!strcmp(a, "page")) { ui_main_goto_tab(doc["page"] | 0); g_srv.send(200, "text/plain", "ok"); }
    else g_srv.send(400, "text/plain", "unknown action");
}

static void handle_ota_done() {
    if (!web_server_ota_armed()) { g_srv.send(403, "text/plain", "OTA locked - arm it on the device (Info page)"); return; }
    bool ok = !Update.hasError();
    g_srv.send(ok ? 200 : 500, "text/plain", ok ? "update ok - rebooting" : "update failed");
    if (ok) { delay(300); ESP.restart(); }
}
static void handle_ota_upload() {
    if (!web_server_ota_armed()) return;
    HTTPUpload& up = g_srv.upload();
    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] start %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[OTA] done, %u bytes\n", up.totalSize); else Update.printError(Serial);
    }
}

void web_server_start() {
    if (g_started) return;
    g_srv.on("/", HTTP_GET, []() { if (!auth()) return; g_srv.send_P(200, "text/html", PAGE); });
    g_srv.on("/api/config", HTTP_OPTIONS, []() { g_srv.sendHeader("Access-Control-Allow-Origin", "*"); g_srv.sendHeader("Access-Control-Allow-Headers", "Content-Type"); g_srv.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS"); g_srv.send(204); });
    g_srv.on("/api/state", HTTP_GET, handle_state);
    g_srv.on("/api/config", HTTP_GET, handle_config_get);
    g_srv.on("/api/config", HTTP_POST, handle_config_post);
    g_srv.on("/api/alerts", HTTP_GET, handle_alerts);
    g_srv.on("/api/stats", HTTP_GET, handle_stats);
    g_srv.on("/api/wifi/scan", HTTP_GET, handle_scan);
    g_srv.on("/api/airports", HTTP_GET, handle_airports);
    g_srv.on("/api/action", HTTP_POST, handle_action);
    g_srv.on("/screen.bmp", HTTP_GET, handle_screen);
    g_srv.on("/metrics", HTTP_GET, handle_metrics);
    g_srv.on("/ota", HTTP_POST, handle_ota_done, handle_ota_upload);
    g_srv.onNotFound([]() { g_srv.send(404, "text/plain", "not found"); });
    g_srv.begin();
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    g_started = true;
    Serial.printf("[WEB] panel at http://%s.local/  (%s)\n", HOSTNAME, WiFi.localIP().toString().c_str());
}
void web_server_loop() { if (g_started) g_srv.handleClient(); }
