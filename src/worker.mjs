// Isolated adapter: no filesystem, serial access, process launch, or harness API.
// The trusted host gateway owns hardware I/O. A missing broker is unavailable.
import http from 'node:http';
let initialized=false,frame=Buffer.alloc(0),busy=false;
function broker(method,body) {
  return new Promise((resolve,reject)=>{
    const raw=JSON.stringify({method,...body});
    const req=http.request({host:'127.0.0.1',port:47772,path:'/plugin',method:'POST',headers:{'Content-Type':'application/json','Content-Length':Buffer.byteLength(raw)}},res=>{
      let data='';res.on('data',c=>{data+=c;if(Buffer.byteLength(data)>16384)res.destroy();});
      res.on('end',()=>{try{const value=JSON.parse(data);if(res.statusCode!==200)reject(Error(value.error));else resolve(value);}catch{reject(Error('invalid_broker_response'));}});
      res.on('error',reject);
    });
    req.setTimeout(2000,()=>req.destroy(Error('broker_timeout')));req.on('error',reject);req.end(raw);
  });
}
const send=value=>{if(process.stdout.writableLength>65536)process.exit(1);process.stdout.write(JSON.stringify(value)+'\n');};
async function handle(r) {
  if(r?.method==='plugin.cancel')return;
  if(!r || r.jsonrpc!=='2.0'||!Number.isSafeInteger(r.id)||r.id<1)return process.exit(1);
  try {
    let result;
    if(r.method==='plugin.initialize'&&!initialized&&r.params?.apiVersion==='1.0.0'&&r.params?.pluginId==='snowball.device-m5stack'){
      initialized=true;result={apiVersion:'1.0.0',pluginId:'snowball.device-m5stack'};
    } else if(!initialized)throw Error('initialization_required');
    else if(r.method==='devices.list') result=await broker('list',{});
    else if(r.method==='devices.render'&&typeof r.params?.deviceId==='string'&&typeof r.params?.text==='string'&&Buffer.byteLength(r.params.text)<=2048&&Object.keys(r.params).every(k=>['deviceId','text'].includes(k))) result=await broker('render',r.params);
    else throw Error('unsupported_request');
    send({jsonrpc:'2.0',id:r.id,result});
  } catch(error) {send({jsonrpc:'2.0',id:r.id,error:{code:-32000,message:error.message==='unsupported_request'?'unsupported_request':'device_broker_unavailable'}});}
}
process.stdin.on('data',chunk=>{
  frame=Buffer.concat([frame,chunk]);if(frame.length>65536)return process.exit(1);
  let end;
  while((end=frame.indexOf(10))>=0){
    const line=frame.subarray(0,end);frame=frame.subarray(end+1);
    let r;try{r=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(line));}catch{return process.exit(1);}
    if(busy){send({jsonrpc:'2.0',id:r.id,error:{code:-32000,message:'busy'}});continue;}
    busy=true;void handle(r).finally(()=>{busy=false;});
  }
});
process.stdin.on('end',()=>process.exit());process.stdin.on('error',()=>process.exit(1));process.stdout.on('error',()=>process.exit(1));
