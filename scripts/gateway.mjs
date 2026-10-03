import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import dgram from 'node:dgram';
import net from 'node:net';
import {spawn} from 'node:child_process';
import {randomBytes} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import readline from 'node:readline';
import {DeviceNavigation} from '../src/navigation.mjs';
import {ReplayGuard,sign,material,privateIp,MAX_FRAME} from '../src/protocol.mjs';

const root=fileURLToPath(new URL('..',import.meta.url));
const args=process.argv.slice(2),options={bind:'127.0.0.1',port:47771,backend:'http://127.0.0.1:8765',python:process.env.SNOWBALL_M5_PYTHON??'python'};
for(let i=0;i<args.length;i++){
  const name=args[i].replace(/^--/,'');
  if(!['bind','port','backend','python','serial','device'].includes(name)||!args[i+1])throw Error('Usage: gateway.mjs [--serial COM7] [--bind PRIVATE_IPV4] [--device PAIRED_DEVICE_IPV4] [--port PORT] [--backend LOOPBACK_URL] [--python PATH]');
  options[name]=args[++i];
}
options.port=+options.port;
if(!privateIp(options.bind)||!Number.isInteger(options.port)||options.port<1024||options.port>65535||!/^(?:COM[1-9][0-9]*|\/dev\/[a-zA-Z0-9._/-]+)$/.test(options.serial??'COM1'))throw Error('invalid_endpoint');
if(!/^http:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}$/.test(options.backend))throw Error('loopback_backend_required');
const directory=path.join(root,'.local');fs.mkdirSync(directory,{recursive:true});
// The hardware enrollment secret never enters the plugin worker or stdout.
const keyFile=path.join(directory,'pairing.key');
if(!fs.existsSync(keyFile))fs.writeFileSync(keyFile,randomBytes(32).toString('hex'),{mode:0o600,flag:'wx'});
const key=fs.readFileSync(keyFile,'utf8').trim();if(!/^[a-f0-9]{64}$/.test(key))throw Error('invalid_pairing_key');
const epoch=randomBytes(16).toString('hex'),guard=new ReplayGuard();
let deviceId=null,deviceAddress=null;try{const saved=JSON.parse(fs.readFileSync(path.join(directory,'device.json'),'utf8'));deviceId=saved.deviceId;deviceAddress=saved.ip;if(!/^m5-[a-f0-9]{12}$/.test(deviceId))deviceId=null;}catch{}
if(options.device)deviceAddress=options.device;
if(deviceAddress&&(!privateIp(deviceAddress)||deviceAddress.startsWith('127.')))throw Error('private_device_address_required');
if(options.device&&deviceId)fs.writeFileSync(path.join(directory,'device.json'),JSON.stringify({deviceId,ip:deviceAddress})+'\n',{mode:0o600});
let serial,lastSeen=0,usbSeen=0,busy=false;
let view={message:'Middleware unavailable',items:[],connected:false};
async function api(route,body) {
  const res=await fetch(options.backend+route,{method:body===undefined?'GET':'POST',headers:{Origin:options.backend,'Content-Type':'application/json','x-snowball-controller':'ctl_'+sign(key,'controller').slice(0,16)},...(body===undefined?{}:{body:JSON.stringify(body)}),signal:AbortSignal.timeout(route==='/v1/harness/models'?20000:4000)});
  const data=await res.json();if(!res.ok)throw Error(data.error??`middleware_http_${res.status}`);return data;
}
const shorten=(value,n=64)=>Array.from(String(value??'')).slice(0,n).join('');
const navigationFile=path.join(directory,'navigation.json');let saved={};try{saved=JSON.parse(fs.readFileSync(navigationFile,'utf8'));}catch{}
const controller=new DeviceNavigation({api,machineName:os.hostname(),saved,save:value=>fs.writeFileSync(navigationFile,JSON.stringify(value)+'\n',{mode:0o600})});
const state=()=>controller.state();
const action=async raw=>view=await controller.action(raw);
function fault(error){view={...view,connected:false,message:shorten(error.message,120)};return view;}
const sendSerial=obj=>{if(serial?.stdin.writable&&serial.stdin.writableLength<65536)serial.stdin.write(JSON.stringify(obj)+'\n');};
if(options.serial){
  serial=spawn(options.python,[path.join(root,'scripts/serial_bridge.py'),'--port',options.serial],{windowsHide:true,stdio:['pipe','pipe','pipe']});
  const lines=readline.createInterface({input:serial.stdout});
  serial.stderr.on('data',()=>console.error('USB broker reported an error; check selected port.'));
  serial.on('error',()=>console.error('USB broker could not start.'));
  serial.on('exit',()=>{usbSeen=0;console.error('USB broker closed. LAN remains available if enrolled.');});
  lines.on('line',async line=>{
    if(line.length>32768)return;
    let r;try{r=JSON.parse(line);}catch{return;}
    if(r.type==='hello'&&/^m5-[a-f0-9]{12}$/.test(r.deviceId)&&r.board==='M5Stack'){
      const first=!usbSeen;
      deviceId=r.deviceId;usbSeen=lastSeen=Date.now();
      const changed=privateIp(r.ip)&&!r.ip.startsWith('127.')&&r.ip!==deviceAddress;
      if(changed)deviceAddress=r.ip;
      if(first||changed)fs.writeFileSync(path.join(directory,'device.json'),JSON.stringify({deviceId,ip:deviceAddress})+'\n',{mode:0o600});
      sendSerial({type:'enroll',key,epoch,host:options.bind,port:options.port});
      if(first)console.log(`Physical device ${deviceId}: FACES=${r.faces?'present':'absent'}, flash=${r.flashBytes}`);return;
    }
    if(r.type!=='request'||!deviceId||r.deviceId!==deviceId||!Number.isInteger(r.id)||r.id<1)return;
    usbSeen=lastSeen=Date.now();
    if(busy)return sendSerial({type:'response',id:r.id,view:{...view,message:'Gateway busy; check status'}});
    busy=true;
    try{sendSerial({type:'response',id:r.id,view:await action(r.action)});}catch(error){sendSerial({type:'response',id:r.id,view:fault(error)});}finally{busy=false;}
  });
}
async function body(req){let chunks=[],size=0;for await(const c of req){size+=c.length;if(size>MAX_FRAME*2)throw Error('body_limit');chunks.push(c);}return JSON.parse(Buffer.concat(chunks).toString('utf8'));}
const respond=(res,status,data)=>{const raw=JSON.stringify(data);res.writeHead(status,{'Content-Type':'application/json','Content-Length':Buffer.byteLength(raw),'Cache-Control':'no-store'});res.end(raw);};
const server=http.createServer(async(req,res)=>{
  req.setTimeout(5000,()=>req.destroy());
  if(req.method!=='POST'||req.url!=='/device'||!privateIp(req.socket.remoteAddress?.replace(/^::ffff:/,'')))return respond(res,403,{error:'endpoint_denied'});
  let frame;
  try{frame=await body(req);const a=guard.accept(key,epoch,frame);const peer=req.socket.remoteAddress?.replace(/^::ffff:/,'');if(peer!==options.bind&&!peer?.startsWith('127.'))lastSeen=Date.now();if(busy)throw Error('gateway_busy');busy=true;
    let result;try{result=await action(a);}catch(error){result=fault(error);}finally{busy=false;}
    const payload=JSON.stringify(result);respond(res,200,{epoch,boot:frame.boot,seq:frame.seq,payload,mac:sign(key,material('response',epoch,frame.boot,frame.seq,payload))});
  }catch{respond(res,403,{error:'request_rejected'});}
});
server.requestTimeout=6000;server.maxConnections=8;
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(options.port,options.bind,resolve);});
// Separate loopback-only broker with a closed list/render surface. Never proxies
// API routes, credentials, sessions.send or filesystem paths to plugin workers.
const broker=http.createServer(async(req,res)=>{
  if(req.method!=='POST'||req.url!=='/plugin'||req.headers.origin!==undefined)return respond(res,403,{error:'denied'});
  try{const b=await body(req);const online=deviceId&&Date.now()-lastSeen<10000;
    if(b.method==='list'&&Object.keys(b).length===1)return respond(res,200,online?[{deviceId,nativeDeviceId:deviceId,label:'M5Stack + FACES',transport:Date.now()-usbSeen<10000?'serial':'lan',supported:true,verifiedIdentity:deviceId,capabilities:['button','select-session','display']}]:[]);
    if(b.method==='render'&&online&&b.deviceId===deviceId&&typeof b.text==='string'&&Buffer.byteLength(b.text)<=2048&&Object.keys(b).every(k=>['method','deviceId','text'].includes(k))){
      if(Date.now()-usbSeen>=10000)throw Error('usb_render_required');
      const id=randomBytes(4).readUInt32LE(0);sendSerial({type:'render',id,text:b.text});
      // This is a queued transport receipt, never proof of display delivery.
      return respond(res,200,{deviceId,delivery:'unacknowledged',requestId:id});
    }
    throw Error('unavailable');
  }catch{respond(res,503,{error:'device_unavailable'});}
});
broker.requestTimeout=5000;broker.maxConnections=4;
await new Promise((resolve,reject)=>{broker.once('error',reject);broker.listen(47772,'127.0.0.1',resolve);});
let udp;
if(!options.bind.startsWith('127.')){
  udp=dgram.createSocket('udp4');let window=0,count=0;
  udp.on('message',async(bytes,remote)=>{
    if(bytes.length>256||!privateIp(remote.address))return;
    if(Date.now()-window>1000){window=Date.now();count=0;}if(++count>10)return;
    let q;try{q=JSON.parse(bytes);}catch{return;}
    if(q.type!=='snowball.discover'||!/^[a-f0-9]{16}$/.test(q.nonce))return;
    try{await state();}catch{return;} // Only announce a reachable real middleware.
    const host=shorten(os.hostname(),32),proof=`discover\n${q.nonce}\n${epoch}\n${options.bind}\n${options.port}\n${host}`;
    udp.send(Buffer.from(JSON.stringify({type:'snowball.gateway',nonce:q.nonce,epoch,ip:options.bind,port:options.port,host,mac:sign(key,proof)})),remote.port,remote.address);
  });
  udp.on('error',()=>console.error('LAN discovery unavailable; USB still works.'));
  udp.bind(47770,'0.0.0.0');
}
console.log(`M5Stack gateway: ${options.bind}:${options.port}; middleware stays on ${options.backend}.`);
let reverse,reverseLogged=false;
function connectReverse(){
  if(reverse||!deviceAddress||!deviceId)return;
  const socket=net.connect({host:deviceAddress,port:47774,...(options.bind.startsWith('127.')?{}:{localAddress:options.bind})});reverse=socket;reverseLogged=false;let buffer='';
  socket.setEncoding('utf8');socket.setNoDelay(true);socket.setTimeout(12000,()=>socket.destroy());
  socket.on('error',()=>{});socket.on('close',()=>{if(reverse===socket)reverse=null;});
  socket.on('data',chunk=>{
    buffer+=chunk;if(Buffer.byteLength(buffer)>32768){socket.destroy();return;}
    let end;while((end=buffer.indexOf('\n'))>=0){const line=buffer.slice(0,end);buffer=buffer.slice(end+1);let frame;try{frame=JSON.parse(line);}catch{socket.destroy();return;}
      if(frame.type==='reverse-challenge'){
        if(frame.deviceId!==deviceId||!/^[a-f0-9]{16}$/.test(frame.nonce)){socket.destroy();return;}
        socket.write(JSON.stringify({type:'reverse-auth',nonce:frame.nonce,epoch,mac:sign(key,`reverse\n${frame.nonce}\n${epoch}`)})+'\n');continue;
      }
      void (async()=>{
        let a;try{a=guard.accept(key,epoch,frame);}catch{socket.destroy();return;}
        lastSeen=Date.now();if(!reverseLogged){reverseLogged=true;console.log(`Authenticated outbound Wi-Fi connection to ${deviceId} (${deviceAddress}).`);}
        let result;if(busy)result={...view,message:'Gateway busy; check status'};
        else {busy=true;try{result=await action(a);}catch(error){result=fault(error);}finally{busy=false;}}
        const payload=JSON.stringify(result);if(!socket.destroyed)socket.write(JSON.stringify({epoch,boot:frame.boot,seq:frame.seq,payload,mac:sign(key,material('response',epoch,frame.boot,frame.seq,payload))})+'\n');
      })();
    }
  });
}
const timer=setInterval(()=>{if(serial?.stdin.writable)sendSerial({type:'probe'});connectReverse();},3000);
connectReverse();
let closing=false;
async function close(){if(closing)return;closing=true;clearInterval(timer);serial?.stdin.end();serial?.kill();reverse?.destroy();udp?.close();server.close();broker.close();}
process.on('SIGINT',()=>void close());process.on('SIGTERM',()=>void close());
