// Read-only verification against the real gateway, middleware and plugin host.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath,pathToFileURL} from 'node:url';
import dgram from 'node:dgram';
import {randomBytes} from 'node:crypto';
import {manifest} from '../src/manifest.mjs';
import {material,sign,equal} from '../src/protocol.mjs';
const root=fileURLToPath(new URL('..',import.meta.url));
const middleware=process.argv[2],ip=process.argv[3];
if(!middleware||!path.isAbsolute(middleware)||!ip)throw Error('Usage: check-live.mjs ABSOLUTE_MIDDLEWARE_DIRECTORY PRIVATE_GATEWAY_IP');
const {PluginHost}=await import(pathToFileURL(path.join(middleware,'packages/plugin-host/dist/index.js')));
const {DeviceRegistry}=await import(pathToFileURL(path.join(middleware,'packages/core/dist/index.js')));
const m=await manifest(),host=new PluginHost({directory:root,manifest:m,approvedDigests:new Map([[m.id,m.integrity.entrySha256]])});
await host.start();
try {
  let list=await host.request('devices.list',{});for(let i=0;i<12&&!list.length;i++){await new Promise(r=>setTimeout(r,500));list=await host.request('devices.list',{});}assert.equal(list.length,1);assert.match(list[0].nativeDeviceId,/^m5-/);
  const registry=new DeviceRegistry(),source={pluginId:m.id,instanceId:'physical'},gen=registry.beginScan(source);
  const observed=registry.observe(source,gen,list[0]);registry.finishScan(source,gen,'ready');const {binding}=registry.register(observed.candidateId,gen);assert.equal(registry.list()[0].state,'ready');
  console.log(JSON.stringify({physicalDevice:list[0].nativeDeviceId,transport:list[0].transport,pluginHost:host.state,registry:'ready'}));
  await assert.rejects(host.request('sessions.send',{text:'never'}));
}finally{await host.stop();}
const key=fs.readFileSync(path.join(root,'.local/pairing.key'),'utf8').trim(),nonce=randomBytes(8).toString('hex');
const socket=dgram.createSocket('udp4');
const reply=await new Promise((resolve,reject)=>{
  const timer=setTimeout(()=>{socket.close();reject(Error('live_discovery_timeout'));},7000);
  socket.on('message',bytes=>{try{const d=JSON.parse(bytes);if(d.nonce!==nonce)return;clearTimeout(timer);socket.close();resolve(d);}catch{}});
  socket.bind(0,()=>socket.send(Buffer.from(JSON.stringify({type:'snowball.discover',nonce})),47770,ip));
});
assert.equal(reply.ip,ip);assert.ok(equal(reply.mac,sign(key,`discover\n${nonce}\n${reply.epoch}\n${reply.ip}\n${reply.port}\n${reply.host}`)));
const boot=randomBytes(8).toString('hex');let seq=0;
async function action(op,index){const payload=JSON.stringify({op,...(index===undefined?{}:{index})});const frame={epoch:reply.epoch,boot,seq:++seq,payload,mac:sign(key,material('request',reply.epoch,boot,seq,payload))};
  const response=await fetch(`http://${ip}:${reply.port}/device`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(frame),signal:AbortSignal.timeout(7000)});assert.equal(response.status,200);const envelope=await response.json();assert.ok(equal(envelope.mac,sign(key,material('response',reply.epoch,boot,seq,envelope.payload))));return {view:JSON.parse(envelope.payload),frame};
}
const poll=await action('poll');assert.equal(poll.view.connected,true);
const sessions=await action('sessions');assert.ok(sessions.view.items.length>0);assert.equal(sessions.view.menuKind,'sessions');
const selected=await action('select',0);assert.ok(selected.view.sessionKey);
let models=await action('models');assert.ok(Array.isArray(models.view.items));
for(let i=0;i<24&&models.view.message==='Discovering native capabilities...';i++){await new Promise(r=>setTimeout(r,1000));models=await action('poll');}
assert.notEqual(models.view.message,'Discovering native capabilities...');
if(models.view.items.length){await action('select',0);let efforts=await action('efforts');for(let i=0;i<24&&efforts.view.message==='Discovering native capabilities...';i++){await new Promise(r=>setTimeout(r,1000));efforts=await action('poll');}assert.ok(Array.isArray(efforts.view.items));console.log(JSON.stringify({models:models.view.items.length,modelEfforts:efforts.view.items.length}));}
const replay=await fetch(`http://${ip}:${reply.port}/device`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(poll.frame)});assert.equal(replay.status,403);
console.log(JSON.stringify({authenticatedDiscovery:true,signedRequestResponse:true,replayRejected:true,nativeSessionPage:sessions.view.items.length,mutatingHarnessCommands:0}));
