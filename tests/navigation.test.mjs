import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DeviceNavigation,windowStart,harnessAbbreviation,wrapLines,sessionText} from '../src/navigation.mjs';
const snapshot={accessMode:'local-no-auth',hostname:'REAL-PC',realSessions:{'snowball.codex':{project:Array.from({length:30},(_,i)=>({id:'n'+i,sessionKey:'k'+i,title:'Session '+i,ownerId:'real-owner',revision:7,readOnly:false}))},'snowball.antigravity':{other:[{id:'agy',sessionKey:'agy-key',title:'AGY session',readOnly:true}]}},commands:[],turnsStore:{k14:[{role:'user',text:'native question',agentResponse:'실제 응답\n'+('line\n'.repeat(100))}]}};
test('selected session is anchored at first, middle or last viewport position',()=>{
  assert.equal(windowStart(30,0),0);assert.equal(windowStart(30,14),11);assert.equal(windowStart(30,29),23);
});
test('machine and harness hierarchy comes from observed sources and keeps five-character labels',async()=>{
  const nav=new DeviceNavigation({api:async()=>snapshot,machineName:'fallback',saved:{sessionKey:'k14'}});
  let v=await nav.action({op:'machines'});assert.deepEqual(v.items,['REAL-PC']);
  v=await nav.action({op:'select',index:0});assert.equal(v.menuKind,'harnesses');assert.deepEqual(v.items,['CODEX','AGY']);
  v=await nav.action({op:'select',index:1});assert.equal(v.menuKind,'sessions');assert.deepEqual(v.items,['AGY session']);
  assert.equal(v.sessionKey,'');assert.equal(harnessAbbreviation('snowball.opencode'),'OCODE');assert.ok(harnessAbbreviation('other.long-harness').length<=5);
});
test('session list preserves the selected native destination and global Home/End indexes',async()=>{
  const nav=new DeviceNavigation({api:async()=>snapshot,machineName:'REAL-PC',saved:{sessionKey:'k14'}});
  let v=await nav.action({op:'sessions'});assert.equal(v.menuIndex,14);assert.equal(v.menuOffset,11);assert.equal(v.items[3],'Session 14');
  v=await nav.action({op:'browse',index:29});assert.equal(v.menuOffset,23);assert.equal(v.items[6],'Session 29');assert.equal(v.sessionKey,'k14');
  v=await nav.action({op:'select',index:29});assert.equal(v.sessionKey,'k29');assert.equal(v.menuKind,'');
});
test('full native content supports bounded pages without presenting previews as conversations',async()=>{
  const nav=new DeviceNavigation({api:async()=>snapshot,machineName:'REAL-PC',saved:{sessionKey:'k14'}});
  let v=await nav.action({op:'read',index:0});assert.equal(v.contentLines[0],'USER');assert.ok(v.contentTotal>100);
  v=await nav.action({op:'read',index:100000});assert.equal(v.contentOffset,v.contentTotal-11);assert.equal(v.contentLines.length,11);
  assert.match(sessionText({}, {preview:'real summary'}),/^PREVIEW/);
  assert.deepEqual(wrapLines('한글 English',8),['한글 Eng','lish']);
});
test('changing harness cancels a pending model result and does not send a native prompt',async()=>{
  let finish,commands=0;const api=async route=>{if(route==='/v1/harness/models')return new Promise(r=>finish=r);if(route==='/v1/commands')commands++;return snapshot;};
  const nav=new DeviceNavigation({api,machineName:'REAL-PC',saved:{sessionKey:'k14'}});
  await nav.action({op:'models'});await nav.action({op:'harnesses'});await nav.action({op:'select',index:1});
  finish({models:[{model:'old-model'}]});await new Promise(r=>setImmediate(r));
  const view=await nav.action({op:'poll'});assert.equal(view.menuKind,'sessions');assert.deepEqual(view.items,['AGY session']);assert.equal(commands,0);
});

test('dispatch rechecks native ownership while capability discovery is pending',async()=>{
  let current=snapshot,finish,commands=0;
  const api=async route=>{if(route==='/v1/harness/models')return new Promise(r=>finish=r);if(route==='/v1/commands')commands++;return current;};
  const nav=new DeviceNavigation({api,machineName:'REAL-PC',saved:{sessionKey:'k14'}});
  await nav.action({op:'models'});
  current=structuredClone(snapshot);current.realSessions['snowball.codex'].project[14].readOnly=true;
  await assert.rejects(nav.action({op:'send',commandId:'a'.repeat(32),text:'must not dispatch'}),/session_not_controllable/);
  assert.equal(commands,0);finish({models:[]});await new Promise(r=>setImmediate(r));
});
