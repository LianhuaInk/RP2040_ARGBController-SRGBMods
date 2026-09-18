import Serial from "@SignalRGB/serial";
export function Name() { return "SRGBmods LED Controller CDC v2.1"; }
export function VendorId() { return 0x16D0; }
export function ProductId() { return 0x1205; }
export function Publisher() { return "Local Fast v2"; }
export function Documentation() { return "gettingstarted/srgbmods-net-info"; }
export function Size() { return [1, 1]; }
export function DefaultPosition() { return [0, 0]; }
export function DefaultScale() { return 1; }
export function Type() { return "serial"; }
export function DeviceType() { return "lightingcontroller"; }
export function SubdeviceController() { return true; }
export function DefaultComponentBrand() { return "CompGen"; }
export function LedNames() { return []; }
export function LedPositions() { return []; }
export function Validate(endpoint) { return endpoint.interface === 1; }

/* global device, shutdownColor, LightingMode, forcedColor, FrameRateTarget,
StatusLED_enable, HWL_enable, HWL_return, HWL_returnafter, HWL_effectMode,
HWL_effectSpeed, HWL_brightness, HWL_color, Diagnostics_enable, Profiling_enable */
export function ControllableParameters() {
  return [
    {property:"shutdownColor", group:"lighting", label:"Shutdown Color", type:"color", default:"#000000"},
    {property:"LightingMode", group:"lighting", label:"Lighting Mode", type:"combobox", values:["Canvas","Forced"], default:"Canvas"},
    {property:"forcedColor", group:"lighting", label:"Forced Color", type:"color", default:"#FF0000"},
    {property:"FrameRateTarget", group:"", label:"Target FPS", type:"combobox", values:["30","60","120"], default:"60"},
    {property:"StatusLED_enable", group:"", label:"Onboard Status LED", type:"boolean", default:"false"},
    {property:"HWL_enable", group:"", label:"Hardware Lighting", type:"boolean", default:"false"},
    {property:"HWL_return", group:"", label:"Return to Hardware Lighting", type:"boolean", default:"false"},
    {property:"HWL_returnafter", group:"", label:"Return Delay (seconds)", type:"number", min:"1", max:"60", step:"1", default:"10"},
    {property:"HWL_effectMode", group:"", label:"Hardware Effect", type:"combobox", values:["Rainbow Wave","Rainbow Cycle","Solid Color","Breathing Color"], default:"Rainbow Wave"},
    {property:"HWL_effectSpeed", group:"", label:"Hardware Speed", type:"number", min:"1", max:"20", step:"1", default:"6"},
    {property:"HWL_brightness", group:"", label:"Hardware Brightness", type:"number", min:"10", max:"255", step:"1", default:"127"},
    {property:"HWL_color", group:"", label:"Hardware Color", type:"color", default:"#800080"},
    {property:"Diagnostics_enable", group:"", label:"Log Device FPS (every 10 seconds)", type:"boolean", default:"false"},
    {property:"Profiling_enable", group:"", label:"Log Host Timings (every 10 seconds)", type:"boolean", default:"true"}
  ];
}

const LED_COUNT = 201, CHANNEL = "Channel 1";
let ready = false, failed = false, frameId = 0, token = 0;
let control = null;
let settingsDirty = true, settingsChanged = 0, previousStats = null, nextStats = 0;
let profile = null, previousRenderStart = null, previousRenderEnd = null;
let appliedTarget = null, wireId = 0, rx = [], pendingTx = null, queuedControl = null;
let lastRx = 0;
let firstRender = true;
function applyTarget() {
  const target = typeof FrameRateTarget === "undefined" ? 60 : bounded(FrameRateTarget,30,120,60);
  if (target === appliedTarget) return;
  if (typeof device.setFrameRateTarget === "function") device.setFrameRateTarget(target);
  else device.log("Target FPS API unavailable; using host default.");
  appliedTarget = target; resetProfile();
  device.log("Requested target FPS="+target+"; actual FPS must be measured.");
}
function crc16(p, length) {
  let crc = 65535;
  for (let j = 0; j < length; ++j) {
    crc ^= p[j] << 8;
    for (let i = 0; i < 8; ++i) crc = ((crc << 1) ^ ((crc & 32768) ? 0x1021 : 0)) & 65535;
  }
  return crc;
}
function envelope(type, p) {
  const id = wireId = (wireId + 1) & 65535;
  const out = [80,82,71,66,1,type,id & 255,id >> 8,p.length & 255,p.length >> 8,...p];
  const crc = crc16(out,out.length);
  out.push(crc & 255,crc >> 8);
  return out;
}
function queueControl(packet) { queuedControl = envelope(2,packet.slice(1)); }
function flushTx(now) {
  if (!pendingTx && queuedControl) {
    pendingTx = {bytes:queuedControl,offset:0,start:now}; queuedControl = null;
  }
  if (!pendingTx) return {write:0,packets:0,maxWrite:0};
  if (now-pendingTx.start > 1000) { stop("CDC write stalled; reconnect the device."); return {write:0,packets:0,maxWrite:0}; }
  const started = clock();
  const tail = pendingTx.bytes.slice(pendingTx.offset);
  const written = Serial.write(tail);
  const duration = Math.max(0,clock()-started);
  if (!Number.isInteger(written) || written < 0 || written > tail.length) {
    stop("CDC write returned an invalid byte count.");
  } else {
    pendingTx.offset += written;
    if (pendingTx.offset === pendingTx.bytes.length) pendingTx = null;
  }
  return {write:duration,packets:1,maxWrite:duration};
}
function readResponse(now) {
  if (rx.length && now-lastRx > 1000) rx = [];
  const incoming = Serial.read(2048,0);
  if (incoming && incoming.length) { rx.push(...incoming); lastRx = now; }
  while (rx.length) {
    if (rx[0] !== 80 || (rx.length > 1 && rx[1] !== 82) || (rx.length > 2 && rx[2] !== 71) || (rx.length > 3 && rx[3] !== 66)) { rx.shift(); continue; }
    if (rx.length < 10) break;
    if (rx[4] !== 1 || rx[5] !== 3 || rx[8] !== 64 || rx[9] !== 0) { rx.shift(); continue; }
    if (rx.length < 76) break;
    if (crc16(rx,74) !== (rx[74] | rx[75]<<8)) { rx.shift(); continue; }
    const payload = rx.slice(10,74); rx.splice(0,76);
    if (control && payload[0] === 0xD2 && payload[1] === (control.command|128) && payload[2] === control.token) return payload;
  }
  return null;
}

function clock() {
  return typeof performance !== "undefined" && typeof performance.now === "function" ? performance.now() : Date.now();
}
function metric() { return {sum:0, max:0, samples:0}; }
function addMetric(m, value) {
  if (!Number.isFinite(value) || value < 0) return;
  m.sum += value; m.max = Math.max(m.max,value); ++m.samples;
}
function average(m) { return m.samples ? (m.sum/m.samples).toFixed(2) : "n/a"; }
function timing(m) { return average(m)+"/"+(m.samples ? m.max.toFixed(2) : "n/a"); }
function resetProfile() { profile = null; previousRenderStart = previousRenderEnd = null; }
function collectProfile(sample, start, end) {
  const mode = sample.compressed ? "RGB444" : "RGB888";
  if (!profile || profile.mode !== mode || start < profile.start) {
    resetProfile();
    profile = {mode, start, frames:0, packets:0, color:metric(), encode:metric(),
      write:metric(), perWrite:metric(), render:metric(), control:metric(), interval:metric(), gap:metric()};
  }
  ++profile.frames; profile.packets += sample.packets;
  addMetric(profile.color,sample.color);
  addMetric(profile.encode,sample.encode);
  addMetric(profile.write,sample.write);
  profile.perWrite.sum += sample.write;
  profile.perWrite.samples += sample.packets;
  profile.perWrite.max = Math.max(profile.perWrite.max,sample.maxWrite);
  addMetric(profile.render,end-start);
  addMetric(profile.control,sample.control);
  if (previousRenderStart !== null) addMetric(profile.interval,start-previousRenderStart);
  if (previousRenderEnd !== null) addMetric(profile.gap,start-previousRenderEnd);
  previousRenderStart = start;
  previousRenderEnd = end;
  if (end-profile.start < 10000) return;
  const fps = profile.interval.sum > 0 ? (1000*profile.interval.samples/profile.interval.sum).toFixed(1) : "n/a";
  device.log("[HostProfile] mode="+mode+", frames="+profile.frames+", packets/frame="+(profile.packets/profile.frames).toFixed(0)+
    ", renderFPS="+fps+", clock="+(typeof performance !== "undefined" && typeof performance.now === "function" ? "performance.now" : "Date.now (1ms resolution)"));
  device.log("[HostProfile] ms avg/max: color="+timing(profile.color)+", encode="+timing(profile.encode)+
    ", CDC/frame="+timing(profile.write)+", CDC/call="+timing(profile.perWrite)+", render="+timing(profile.render));
  device.log("[HostProfile] ms avg/max: startInterval="+timing(profile.interval)+", outsideRenderGap="+timing(profile.gap)+
    ", control="+timing(profile.control)+"; outsideRenderGap includes host scheduling/work, not a measured sleep timer.");
  // Exclude summary logging itself from the next window's scheduler-gap estimate.
  resetProfile();
}

function enabled(value) { return value === true || value === "true"; }
function bounded(value, min, max, fallback) {
  const n = Number(value);
  return Number.isFinite(n) ? Math.max(min, Math.min(max, Math.round(n))) : fallback;
}
function rgb(hex) {
  const match = /^#?([\da-f]{2})([\da-f]{2})([\da-f]{2})$/i.exec(hex || "");
  return match ? [parseInt(match[1],16),parseInt(match[2],16),parseInt(match[3],16)] : [0,0,0];
}
function settingsBytes() {
  const effects = ["Rainbow Wave","Rainbow Cycle","Solid Color","Breathing Color"];
  return [enabled(HWL_enable) ? 1 : 0, enabled(HWL_return) ? 1 : 0,
    bounded(HWL_returnafter,1,60,10), Math.max(1,effects.indexOf(HWL_effectMode)+1),
    bounded(HWL_effectSpeed,1,20,6), bounded(HWL_brightness,10,255,127),
    ...rgb(HWL_color), enabled(StatusLED_enable) ? 1 : 0];
}
function writeControl(command, data, now) {
  const packet = new Array(65).fill(0);
  packet[1] = 0xD2; packet[2] = command; packet[3] = token = (token + 1) & 255;
  for (let i = 0; i < data.length; ++i) packet[5+i] = data[i];
  control = {command, token, packet, sent:now, attempts:1};
  queueControl(packet);
}
function stop(message) {
  failed = true; ready = false; control = null;
  pendingTx = queuedControl = null;
  Serial.disconnect();
  device.log(message);
  device.notify("Fast v2 connection failed",message,0);
}
function uint32(p, offset) {
  return (p[offset] | (p[offset+1]<<8) | (p[offset+2]<<16) | (p[offset+3]<<24)) >>> 0;
}
function recordStats(p) {
  const sample = {ms:uint32(p,44), complete:uint32(p,28), shown:uint32(p,32)};
  if (previousStats) {
    const seconds = ((sample.ms-previousStats.ms)>>>0)/1000;
    if (seconds > 0 && seconds < 60) {
      device.log("CDC v2.1: complete="+(((sample.complete-previousStats.complete)>>>0)/seconds).toFixed(1)+
        " FPS, displayed="+(((sample.shown-previousStats.shown)>>>0)/seconds).toFixed(1)+
        " FPS; cumulative queueDrops="+uint32(p,20)+", invalid="+uint32(p,24)+
        ", replaced="+uint32(p,36)+", incomplete="+uint32(p,40));
    }
  }
  previousStats = sample;
}
function pollControl(now) {
  const p = readResponse(now);
  if (!control) return;
  if (p && p[1] === (control.command|0x80) && p[2] === control.token) {
    const command = control.command;
    control = null;
    if (p[3] !== 0) { stop("Firmware rejected command or LED driver initialization failed."); return; }
    if (command === 1) {
      if (p[4] !== 2 || !p[15] || p[7] !== 1 || (p[8]|p[9]<<8) !== LED_COUNT || !p[12] ||
          p[10] !== 7 || p[11] !== 48 || [24,40,32,20,48,12,25].some((n,i)=>p[48+i]!==n)) {
        stop("Install CDC v2.1 firmware with the 201-LED / 7-port configuration."); return;
      }
      ready = true;
      settingsDirty = true;
      settingsChanged = now - 3000;
      nextStats = now + 10000;
      device.log("CDC connected: firmware "+p[4]+"."+p[5]+"."+p[6]+", 201 LEDs.");
      if (!p[14]) device.log("Warning: EEPROM unavailable; settings cannot persist.");
      recordStats(p);
    } else if (command === 3) recordStats(p);
    return;
  }
  if (now-control.sent >= 250) {
    if (control.attempts >= 12) {
      if (control.command === 3) { device.log("CDC statistics request timed out."); control = null; }
      else stop("No CDC acknowledgement. Check CDC v2.1 firmware, COM port and interface 1.");
      return;
    }
    if (!queuedControl && !pendingTx) queueControl(control.packet);
    control.sent = now;
    ++control.attempts;
  }
}

export function Initialize() {
  firstRender = true;
  Serial.disconnect();
  Serial.connect({baudRate:115200,dataBits:8,stopBits:"One",parity:"None"});
  appliedTarget = null; applyTarget();
  wireId = 0; rx = []; pendingTx = queuedControl = null;
  ready = failed = false;
  frameId = token = 0;
  control = previousStats = null;
  settingsDirty = true;
  resetProfile();
  const started = Date.now();
  device.SetLedLimit(LED_COUNT);
  device.addChannel(CHANNEL,LED_COUNT);
  if (!Serial.isConnected()) { stop("CDC connection failed. Install CDC v2.1 firmware and disable the HID plugin."); return; }
  writeControl(1,[],started);
  flushTx(started);
  device.log("CDC v2.1 loaded: RGB888 whole-frame transport; paired CDC firmware required.");
}

function sendFrame(overrideColor, measure = false) {
  const colorStart = measure ? clock() : 0;
  const channel = device.channel(CHANNEL);
  let colors, count = bounded(channel.ledCount,0,LED_COUNT,0);
  const pulse = !overrideColor && LightingMode !== "Forced" && channel.shouldPulseColors();
  if (overrideColor) colors = device.createColorArray(overrideColor,LED_COUNT,"Inline");
  else if (LightingMode === "Forced") colors = device.createColorArray(forcedColor,LED_COUNT,"Inline");
  else if (pulse) colors = device.createColorArray(device.getChannelPulseColor(CHANNEL),LED_COUNT,"Inline");
  else colors = channel.getColors("Inline");
  if (overrideColor || LightingMode === "Forced" || pulse) count = LED_COUNT;
  const colorEnd = measure ? clock() : 0;
  const encodeStart = clock();
  // Finish any partial packet before submitting another frame. Do not build a backlog.
  let transfer;
  if (pendingTx || queuedControl) transfer = flushTx(Date.now());
  else {
    const payload = new Array(LED_COUNT*3).fill(0);
    for (let i = 0; i < count*3; ++i) payload[i] = (colors[i] || 0) & 255;
    pendingTx = {bytes:envelope(1,payload),offset:0,start:Date.now()};
    const encodeEnd = clock();
    transfer = flushTx(Date.now());
    if (measure) return Object.assign({},transfer,{compressed:false,color:Math.max(0,colorEnd-colorStart),encode:Math.max(0,encodeEnd-encodeStart)});
  }
  if (measure) return Object.assign({},transfer,{compressed:false,color:Math.max(0,colorEnd-colorStart),encode:0});
}

export function Render() {
  if (firstRender) { appliedTarget = null; firstRender = false; }
  applyTarget();
  if (failed) return;
  if (!Serial.isConnected()) { stop("CDC disconnected. Reload the plugin after reconnecting."); return; }
  const measure = typeof Profiling_enable !== "undefined" && enabled(Profiling_enable);
  const start = measure ? clock() : 0;
  if (!measure) resetProfile();
  const now = Date.now();
  pollControl(now);
  if (failed) return;
  if (!ready) { flushTx(now); return; }
  if (!control && settingsDirty && now-settingsChanged >= 3000) {
    settingsDirty = false;
    writeControl(2,settingsBytes(),now);
  } else if (!control && enabled(Diagnostics_enable) && now >= nextStats) {
    writeControl(3,[],now);
    nextStats = now+10000;
  }
  const beforeFrame = measure ? clock() : 0;
  const sample = sendFrame(undefined,measure);
  if (measure) {
    const end = clock();
    sample.control = Math.max(0,beforeFrame-start);
    collectProfile(sample,start,end);
  }
}
export function Shutdown(SystemSuspending) {
  if (ready && !failed && !pendingTx && !queuedControl) sendFrame(SystemSuspending ? "#000000" : shutdownColor);
  Serial.disconnect();
}
function changed() { settingsDirty = true; settingsChanged = Date.now(); }
export function onHWL_enableChanged() { changed(); }
export function onHWL_returnChanged() { changed(); }
export function onHWL_returnafterChanged() { changed(); }
export function onHWL_effectModeChanged() { changed(); }
export function onHWL_effectSpeedChanged() { changed(); }
export function onHWL_brightnessChanged() { changed(); }
export function onHWL_colorChanged() { changed(); }
export function onStatusLED_enableChanged() { changed(); }
export function onProfiling_enableChanged() { resetProfile(); }
