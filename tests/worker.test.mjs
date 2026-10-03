import {test} from 'node:test';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import readline from 'node:readline';
import {manifest} from '../src/manifest.mjs';
test('real isolated worker initializes and refuses harness/filesystem operations',async t=>{
  const child=spawn(process.execPath,['src/worker.mjs'],{windowsHide:true,stdio:['pipe','pipe','pipe']});t.after(()=>child.kill());child.stderr.resume();
  let id=0;const waiting=new Map();const lines=readline.createInterface({input:child.stdout});
  lines.on('line',line=>{const value=JSON.parse(line);waiting.get(value.id)?.(value);waiting.delete(value.id);});
  function request(method,params){const requestId=++id;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('worker_timeout')),3000);waiting.set(requestId,value=>{clearTimeout(timer);resolve(value);});child.stdin.write(JSON.stringify({jsonrpc:'2.0',id:requestId,method,params})+'\n');});}
  assert.ok((await request('devices.list',{})).error);
  assert.equal((await request('plugin.initialize',{apiVersion:'1.0.0',pluginId:'snowball.device-m5stack'})).result.pluginId,'snowball.device-m5stack');
  assert.ok((await request('sessions.send',{text:'must not execute'})).error);
  assert.ok((await request('devices.render',{deviceId:'m5-fake',text:'x',path:'C:/'})).error);
  assert.ok((await request('filesystem.read',{path:'C:/'})).error);
  const m=await manifest();assert.equal(m.kind,'hardware');assert.deepEqual(m.permissions,[]);assert.equal(m.integrity.entrySha256.length,64);
  assert.ok(m.capabilities.every(c=>c.operation.startsWith('devices.')));
  child.stdin.end();
});
