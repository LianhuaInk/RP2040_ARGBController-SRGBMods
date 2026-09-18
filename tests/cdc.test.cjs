const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname,'../plugin/Pico_SRGB_CDC_v2_1.js'),'utf8');
assert.ok(!/\{\s*\.\.\./.test(source),'SignalRGB runtime does not support object spread');
const exportsList = [...source.matchAll(/^export function (\w+)/gm)].map(m=>m[1]);
const colors = Array.from({length:603},(_,i)=>(i*37+11)&255);
function host() {
  let now=10000, connected=false, maxWrite=9999;
  const bytes=[], input=[], logs=[], targets=[];
  const channel={ledCount:201,getColors:()=>colors,shouldPulseColors:()=>false};
  const Serial={connect:()=>connected=true,disconnect:()=>connected=false,isConnected:()=>connected,
    write(p) { const n=Math.min(maxWrite,p.length); bytes.push(...p.slice(0,n)); return n; },
    read(n,timeout) { assert.equal(timeout,0); return input.splice(0,n); }};
  const device={SetLedLimit:n=>assert.equal(n,201),addChannel:()=>{},channel:()=>channel,
    setFrameRateTarget:n=>targets.push(n),log:m=>logs.push(m),notify:()=>{},
    createColorArray:(hex,n)=>Array.from({length:n*3},(_,i)=>parseInt(hex.slice(1+i%3*2,3+i%3*2),16))};
  const c=vm.createContext({Serial,device,Date:{now:()=>now},FrameRateTarget:'60',
    LightingMode:'Canvas',shutdownColor:'#123456',forcedColor:'#FF0000',
    HWL_enable:false,HWL_return:false,HWL_returnafter:10,HWL_effectMode:'Rainbow Wave',
    HWL_effectSpeed:6,HWL_brightness:127,HWL_color:'#800080',StatusLED_enable:false,
    Diagnostics_enable:false,Profiling_enable:true});
  vm.runInContext(source.replace(/^import .*;\r?\n/m,'').replace(/^export /gm,'')+'\nthis.api={'+exportsList.join(',')+'};',c);
  function tick(ms=10) { now+=ms; c.api.Render(); }
  function reply(capability=1) {
    const request=bytes.slice(-76), p=new Array(64).fill(0);
    p[0]=0xD2; p[1]=request[11]|128; p[2]=request[12]; p[4]=2; p[5]=1;
    p[7]=1;p[8]=201;p[10]=7;p[11]=48;p[12]=1;p[14]=1;p[15]=capability;
    [24,40,32,20,48,12,25].forEach((n,i)=>p[48+i]=n);
    return Array.from(c.envelope(3,p));
  }
  return {c,bytes,input,logs,targets,channel,tick,reply,setMax:n=>maxWrite=n};
}
function frames(bytes) {
  const result=[];
  for (let i=0;i<bytes.length;) {
    assert.deepEqual(bytes.slice(i,i+4),[80,82,71,66]);
    const n=bytes[i+8]|bytes[i+9]<<8;
    assert.ok(i+n+12<=bytes.length,'no truncated frame');
    result.push(bytes.slice(i,i+n+12)); i+=n+12;
  }
  return result;
}
const h=host(); h.c.api.Initialize();
assert.deepEqual(h.targets,[60]); assert.equal(h.bytes.length,76);
h.tick(); assert.equal(h.bytes.length,76,'no colors before handshake');
const response=h.reply(); h.input.push(0,99,...response.slice(0,19));h.tick();
assert.equal(h.bytes.length,76,'fragmented handshake waits');
h.input.push(...response.slice(19)); h.tick();h.tick();
let packets=frames(h.bytes);
const frame=packets.find(p=>p[5]===1);
assert.equal(frame.length,615);assert.deepEqual(frame.slice(10,613),colors);
assert.equal(h.c.crc16(frame,613),frame[613]|frame[614]<<8);
h.bytes.length=0;h.channel.ledCount=57;h.tick();
assert.deepEqual(frames(h.bytes)[0].slice(10,613),[...colors.slice(0,171),...new Array(432).fill(0)]);
h.bytes.length=0;h.setMax(37);h.tick();
assert.equal(h.bytes.length,37);
for(let i=0;i<16;i++)h.tick();
assert.equal(h.bytes.length,615,'short writes complete the same packet without interleaving');
frames(h.bytes);h.setMax(9999);h.bytes.length=0;h.c.FrameRateTarget='120';h.tick();
assert.deepEqual(h.targets,[60,60,120]);
h.bytes.length=0;h.c.LightingMode='Forced';h.tick();
assert.deepEqual(frames(h.bytes)[0].slice(10,613),Array.from({length:603},(_,i)=>i%3===0?255:0));
const old=host();old.c.api.Initialize();old.input.push(...old.reply(0));old.tick();
assert.ok(old.logs.some(m=>m.includes('Install CDC')));assert.equal(old.bytes.length,76);
const corrupt=host();corrupt.c.api.Initialize();const bad=corrupt.reply();bad[22]^=1;
corrupt.input.push(...bad);corrupt.tick();assert.equal(corrupt.bytes.length,76);
corrupt.input.push(...corrupt.reply());corrupt.tick();assert.ok(corrupt.bytes.length>76);
const stalled=host();stalled.setMax(0);stalled.c.api.Initialize();stalled.tick(1100);
assert.ok(stalled.logs.some(m=>m.includes('stalled')));
if(process.argv[2])fs.writeFileSync(process.argv[2],Buffer.from(frame));
console.log('PASS: CDC handshake, fragmented/corrupt replies, capability rejection, RGB888/zero tail/forced colors, one-call frame, short writes, stall timeout and target-FPS changes.');
