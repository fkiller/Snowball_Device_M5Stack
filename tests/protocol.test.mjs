import {test} from 'node:test';
import assert from 'node:assert/strict';
import {ReplayGuard,material,sign,flattenSessions,validateAction,privateIp} from '../src/protocol.mjs';
const key='a'.repeat(64),epoch='b'.repeat(32),boot='c'.repeat(16);
function frame(seq,payload=JSON.stringify({op:'poll'})){return {epoch,boot,seq,payload,mac:sign(key,material('request',epoch,boot,seq,payload))};}
test('authenticated request rejects tampering, replay, and a previous gateway epoch',()=>{
  const guard=new ReplayGuard();assert.deepEqual(guard.accept(key,epoch,frame(1)),{op:'poll'});
  assert.throws(()=>guard.accept(key,epoch,frame(1)),/replay/);
  assert.throws(()=>guard.accept(key,epoch,{...frame(2),payload:'{"op":"send"}'}),/authentication/);
  assert.throws(()=>guard.accept(key,'d'.repeat(32),frame(2)),/invalid_frame/);
  assert.deepEqual(guard.accept(key,epoch,frame(3)),{op:'poll'});
  assert.throws(()=>guard.accept(key,epoch,frame(2)),/replay/);
});
test('response signature cannot serve as a request signature',()=>{
  const f=frame(1);f.mac=sign(key,material('response',epoch,boot,1,f.payload));assert.throws(()=>new ReplayGuard().accept(key,epoch,f),/authentication/);
});
test('device surface has no paths, shell, arbitrary operation or implicit send identifier',()=>{
  assert.throws(()=>validateAction({op:'exec',text:'whoami'}),/unsupported/);
  assert.throws(()=>validateAction({op:'poll',path:'C:/'}),/invalid/);
  assert.throws(()=>validateAction({op:'send',text:'hello'}),/invalid/);
  assert.throws(()=>validateAction({op:'select',index:-1}),/invalid/);
  assert.equal(validateAction({op:'send',text:'한글 English',commandId:'a'.repeat(32)}).text,'한글 English');
});
test('session ownership and revision come from the live journal, without invented sessions',()=>{
  assert.deepEqual(flattenSessions({}),[]);
  const result=flattenSessions({realSessions:{'snowball.codex':{project:[{id:'native',sessionKey:'key',title:'한글',ownerId:'stale'}]}},sessions:[{sessionKey:'key',ownerId:'live',revision:4}]});
  assert.equal(result[0].ownerId,'live');assert.equal(result[0].revision,4);assert.equal(result[0].title,'한글');
});
test('gateway accepts literal local/private IPv4 only',()=>{
  for(const ip of ['127.0.0.1','192.168.1.197','10.0.0.1','172.16.1.2'])assert.equal(privateIp(ip),true);
  for(const ip of ['0.0.0.0','8.8.8.8','172.32.1.2','localhost'])assert.equal(privateIp(ip),false);
});
