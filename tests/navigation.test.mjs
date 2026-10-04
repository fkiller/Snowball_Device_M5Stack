import {test} from 'node:test';
import assert from 'node:assert/strict';
import {DeviceNavigation,windowStart,harnessAbbreviation,wrapLines,sessionText,latestSession} from '../src/navigation.mjs';
const snapshot={accessMode:'local-no-auth',hostname:'REAL-PC',realSessions:{'snowball.codex':{project:Array.from({length:30},(_,i)=>({id:'n'+i,sessionKey:'k'+i,title:'Session '+i,ownerId:'real-owner',revision:7,readOnly:false}))},'snowball.antigravity':{other:[{id:'agy',sessionKey:'agy-key',title:'AGY session',readOnly:true}]}},commands:[],turnsStore:{k14:[{role:'user',text:'native question',agentResponse:'실제 응답\n'+('line\n'.repeat(100))}]}};
test('display locale remains controller-local and preserves native text and targets',async()=>{
  const a=new DeviceNavigation({api:async()=>snapshot,saved:{sessionKey:'k14'}});
  const b=new DeviceNavigation({api:async()=>snapshot,saved:{sessionKey:'k14'}});
  const ko=await a.action({op:'read',index:0,locale:'ko'});
  assert.equal(ko.message,'준비됨');assert.equal(ko.contentLines[0],'사용자');
  assert.ok(ko.contentLines.includes('native question'));assert.ok(ko.contentLines.includes('실제 응답'));
  assert.equal(ko.session,'Session 14');assert.equal(ko.sessionKey,'k14');
  const en=await b.action({op:'poll'});assert.equal(en.message,'Ready');assert.equal(en.contentLines[0],'USER');
  a.persist();assert.equal(a.saved.locale,'ko');
  assert.equal((await a.action({op:'poll',locale:'en'})).contentLines[0],'USER');
  await assert.rejects(a.action({op:'poll',locale:'other'}),/invalid_locale/);
});
test('capability progress is structural and follows the current display locale',async()=>{
  let finish;
  const a=new DeviceNavigation({api:async route=>route==='/v1/harness/models'?new Promise(resolve=>finish=resolve):snapshot,saved:{sessionKey:'k14'}});
  let view=await a.action({op:'models',locale:'en'});assert.equal(view.catalogPending,true);
  view=await a.action({op:'poll',locale:'ko'});assert.equal(view.catalogPending,true);assert.equal(view.message,'네이티브 기능 검색 중...');
  finish({models:[]});await new Promise(resolve=>setImmediate(resolve));
  view=await a.action({op:'poll'});assert.equal(view.catalogPending,false);assert.equal(view.message,'보고된 네이티브 기능 없음');
  view=await a.action({op:'poll',locale:'en'});assert.equal(view.message,'No native capabilities reported');
});
test('two devices retain independent selection, theme, model and scroll across own restart',async()=>{
  let saved;const make=options=>new DeviceNavigation({api:async()=>snapshot,machineName:'REAL-PC',...options});
  const a=make({saved:{sessionKey:'k14',model:'native-choice',effort:'native-effort'},save:value=>saved=structuredClone(value)}),b=make({saved:{sessionKey:'k0'}});
  await a.action({op:'skin'});await a.action({op:'read',index:50});a.persist();
  const second=await b.action({op:'poll'});assert.equal(second.sessionKey,'k0');assert.equal(second.skinId,'slate-dark');assert.equal(second.contentOffset,0);
  const restored=await make({saved}).action({op:'poll'});assert.equal(restored.sessionKey,'k14');assert.equal(restored.skinId,'high-contrast');assert.equal(restored.model,'native-choice');assert.equal(restored.contentOffset,50);
});
test('selected session is anchored at first, middle or last viewport position',()=>{
  assert.equal(windowStart(30,0),0);assert.equal(windowStart(30,14),11);assert.equal(windowStart(30,29),23);
});
test('machine, harness, project and session hierarchy comes from observed sources',async()=>{
  const nav=new DeviceNavigation({api:async()=>snapshot,machineName:'fallback',saved:{sessionKey:'k14'}});
  let v=await nav.action({op:'machines'});assert.deepEqual(v.items,['REAL-PC']);
  v=await nav.action({op:'select',index:0});assert.equal(v.menuKind,'harnesses');assert.deepEqual(v.items,['CODEX','AGY']);
  v=await nav.action({op:'select',index:1});assert.equal(v.menuKind,'projects');assert.deepEqual(v.items,['other']);assert.equal(v.sessionKey,'agy-key');
  v=await nav.action({op:'select',index:0});assert.equal(v.menuKind,'sessions');assert.deepEqual(v.items,['AGY session']);
  assert.equal(v.project,'other');assert.equal(harnessAbbreviation('snowball.opencode'),'OCODE');assert.ok(harnessAbbreviation('other.long-harness').length<=5);
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
  const view=await nav.action({op:'poll'});assert.equal(view.menuKind,'projects');assert.deepEqual(view.items,['other']);assert.equal(commands,0);
});

test('unselected controllers pick actual latest activity while an existing selection stays private',async()=>{
  const s=structuredClone(snapshot);s.realSessions['snowball.codex'].project[3].updatedAt='2026-10-03T18:00:00Z';
  s.realSessions['snowball.antigravity'].other[0].updatedAt=1791051000;
  s.commands=[{sessionKey:'k7',updatedAt:1791055000000}];
  assert.equal(latestSession(s).sessionKey,'k7');
  const nav=new DeviceNavigation({api:async()=>s,machineName:'actual'});let v=await nav.action({op:'poll'});assert.equal(v.sessionKey,'k7');assert.equal(v.project,'project');
  s.commands.push({sessionKey:'agy-key',updatedAt:1791059000000});v=await nav.action({op:'poll'});assert.equal(v.sessionKey,'k7');
  const other=await new DeviceNavigation({api:async()=>s,machineName:'actual'}).action({op:'poll'});assert.equal(other.sessionKey,'agy-key');
  const missing=await new DeviceNavigation({api:async()=>s,machineName:'actual',saved:{sessionKey:'removed-session',model:'stale-model',effort:'stale-effort',scroll:50}}).action({op:'poll'});
  assert.equal(missing.sessionKey,'agy-key');assert.equal(missing.model,'Native default');assert.equal(missing.effort,'Native default');assert.equal(missing.contentOffset,0);
  const empty=await new DeviceNavigation({api:async()=>({accessMode:'local-no-auth',realSessions:{}}),machineName:'actual'}).action({op:'poll'});assert.equal(empty.session,'');
});

test('projects keep identical display names at distinct actual paths separate and consume plugin icons',async()=>{
  const s=structuredClone(snapshot);s.realSessions['snowball.codex'].project[0].cwd='C:/one/project';s.realSessions['snowball.codex'].project[1].cwd='C:/two/project';
  s.harnessPresentations={'snowball.codex':{name:'Codex',icon:{size:16,rows:Array(16).fill(0x1234)}}};
  const nav=new DeviceNavigation({api:async()=>s,machineName:'actual',saved:{sessionKey:'k0'}});
  const v=await nav.action({op:'projects'});assert.equal(v.menuIndex,0);assert.equal(v.harnessName,'Codex');assert.deepEqual(v.harnessIcon,s.harnessPresentations['snowball.codex'].icon);
  const sessions=await nav.action({op:'select',index:1});assert.equal(sessions.menuKind,'sessions');assert.deepEqual(sessions.items,['Session 1']);assert.equal(sessions.sessionKey,'k1');
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
