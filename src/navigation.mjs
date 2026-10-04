import {createHash} from 'node:crypto';
import {flattenSessions,validateAction} from './protocol.mjs';
import {translate,validLocale} from './i18n.mjs';

export const LIST_ROWS=7, CONTENT_ROWS=11;
const projectKey=s=>String(s.cwd??s.project??'');
export function activityTime(value){
  if(typeof value==='number'&&Number.isFinite(value))return value<1e11?value*1000:value;
  if(typeof value==='string'){if(/^\d+(\.\d+)?$/.test(value))return activityTime(Number(value));const n=Date.parse(value);if(Number.isFinite(n))return n;}
  return 0;
}
export function latestSession(snapshot,sessions=flattenSessions(snapshot)){
  const commands=new Map();for(const c of snapshot.commands??[])commands.set(c.sessionKey,Math.max(commands.get(c.sessionKey)??0,activityTime(c.updatedAt)));
  return sessions.reduce((latest,s)=>{
    const time=Math.max(activityTime(s.updatedAt),activityTime(s.lastActivityAt),commands.get(s.sessionKey)??0);
    return !latest||time>latest.time?{session:s,time}:latest;
  },null)?.session??null;
}
export function windowStart(total,index,rows=LIST_ROWS){return Math.max(0,Math.min(index-Math.floor(rows/2),total-rows));}
export function harnessAbbreviation(id){
  const name=String(id).replace(/^snowball\./,'');
  return ({codex:'CODEX',antigravity:'AGY',opencode:'OCODE'})[name]??Array.from(name.toUpperCase()).slice(0,5).join('');
}
export function wrapLines(text,columns=42){
  const lines=[];
  for(const original of String(text).replace(/\r/g,'').split('\n')){
    let line='',width=0;
    for(const character of original.replace(/\t/g,'    ')){
      const code=character.codePointAt(0),units=(code>=0x1100&&(code<=0x115f||code>=0x2e80))?2:1;
      if(width+units>columns){lines.push(line);line='';width=0;}
      line+=character;width+=units;
    }
    lines.push(line);
  }
  return lines;
}
export function sessionText(snapshot,current,locale='en'){
  if(!current)return '';
  const turns=snapshot.turnsStore?.[current.sessionKey]??snapshot.turnsStore?.[current.id]??snapshot.sessionMessages?.[current.sessionKey]??[];
  const messages=[];
  for(const turn of turns){
    const text=turn.text??turn.content;
    if(typeof text==='string'&&text)messages.push(`${translate(locale,String(turn.role??'message').toLowerCase())}\n${text}`);
    if(typeof turn.agentResponse==='string'&&turn.agentResponse&&turn.agentResponse!==text)messages.push(`${translate(locale,'assistant')}\n${turn.agentResponse}`);
  }
  return messages.length?messages.join('\n\n'):current.preview?`${translate(locale,'preview')}\n${current.preview}`:'';
}
export class DeviceNavigation {
  constructor({api,machineName,saved={},save=()=>{}}){
    this.locale=validLocale(saved.locale)?saved.locale:'en';
    Object.assign(this,{api,machineName,save,saved,selection:null,harness:saved.harness??null,model:saved.model??null,effort:saved.effort??null,
      project:null,menu:[],menuKind:'',menuIndex:0,lastCommand:null,skin:saved.skin??'slate-dark',override:null,snapshotCache:null,
      catalogPending:false,catalogGeneration:0,contentIndex:saved.scroll??0,contentLines:[],contentHash:''});
  }
  async state(){
    const snapshot=await this.api('/v1/snapshot');
    if(snapshot.accessMode!=='local-no-auth')throw Error('middleware_token_mode_not_supported');
    this.snapshotCache=snapshot;
    const sessions=flattenSessions(snapshot);
    if(this.selection){this.selection=sessions.find(s=>s.sessionKey===this.selection.sessionKey)??null;}
    if(!this.initialized){
      this.initialized=true;
      const restored=sessions.find(s=>s.sessionKey===this.saved.sessionKey);
      this.selection=restored??latestSession(snapshot,sessions);
      if(!restored){this.model=this.effort=null;this.contentIndex=0;this.lastCommand=null;}
      this.harness=this.selection?.pluginId??this.harness;
      this.project=this.selection?projectKey(this.selection):null;
    }
    if(!this.selection&&!this.menuKind){this.selection=latestSession(snapshot,sessions);this.harness=this.selection?.pluginId??this.harness;this.project=this.selection?projectKey(this.selection):null;this.model=this.effort=null;this.contentIndex=0;this.lastCommand=null;}
    return snapshot;
  }
  persist(){this.saved={...this.saved,locale:this.locale,sessionKey:this.selection?.sessionKey??null,harness:this.harness,model:this.model,effort:this.effort,skin:this.skin,scroll:this.contentIndex};this.save(this.saved);}
  harnesses(s){
    return [...new Set([...Object.keys(s.realSessions??{}),...(s.connectedHarnesses??[]).map(h=>h.pluginId),
      ...flattenSessions(s).map(h=>h.pluginId)].filter(Boolean))].map(pluginId=>({label:s.harnessPresentations?.[pluginId]?.name??harnessAbbreviation(pluginId),pluginId}));
  }
  sessionView(s){
    const current=this.selection;
    const text=sessionText(s,current,this.locale),hash=createHash('sha256').update((current?.sessionKey??'')+'\n'+text).digest('hex');
    if(hash!==this.contentHash){this.contentHash=hash;this.contentLines=text?wrapLines(text):[];}
    this.contentIndex=Math.max(0,Math.min(this.contentIndex,Math.max(0,this.contentLines.length-CONTENT_ROWS)));
    const command=this.lastCommand?(s.commands??[]).find(c=>c.commandId===this.lastCommand):null;
    const offset=windowStart(this.menu.length,this.menuIndex);
    const presentation=s.harnessPresentations?.[this.harness??current?.pluginId];
    return {connected:true,machine:String(s.hostname??this.machineName),harness:harnessAbbreviation(this.harness??current?.pluginId??''),
      harnessName:presentation?.name??harnessAbbreviation(this.harness??current?.pluginId??''),harnessIcon:presentation?.icon??null,
      project:String(current?.project??this.menu.find(it=>it.key===this.project)?.label??''),
      sessionKey:current?.sessionKey??'',session:String(current?.title??current?.nativeSessionId??''),
      destination:String(current?.title??''),model:String(this.model??current?.model??translate(this.locale,'nativeDefault')).slice(0,40),
      effort:String(this.effort??current?.effort??translate(this.locale,'nativeDefault')).slice(0,24),readOnly:current?.readOnly??true,
      commandId:this.lastCommand??'',commandStatus:command?.status??(this.lastCommand?'unconfirmed':'idle'),message:this.localizedOverride()??(command?`${translate(this.locale,'command')}: ${command.status}`:translate(this.locale,'ready')),
      items:this.menu.slice(offset,offset+LIST_ROWS).map(it=>Array.from(it.label).slice(0,80).join('')),
      itemIcons:this.menu.slice(offset,offset+LIST_ROWS).map(it=>s.harnessPresentations?.[it.pluginId]?.icon??null),
      menuKind:this.menuKind,menuTotal:this.menu.length,menuOffset:offset,menuIndex:this.menuIndex,catalogPending:this.catalogPending,
      contentLines:this.contentLines.slice(this.contentIndex,this.contentIndex+CONTENT_ROWS),contentOffset:this.contentIndex,
      contentTotal:this.contentLines.length,contentHash:this.contentHash,skinId:this.skin,skinName:translate(this.locale,this.skin==='slate-dark'?'slate':'contrast')};
  }
  localizedOverride(){
    if(!this.override)return null;
    const {key,suffix,detail}=this.override;
    return translate(this.locale,key)+(suffix?`: ${suffix}`:'')+(detail?`. ${translate(this.locale,detail)}`:'');
  }
  list(kind,items,index=0){
    ++this.catalogGeneration;this.catalogPending=false;this.menuKind=kind;this.menu=items;
    this.menuIndex=Math.max(0,Math.min(index,items.length-1));this.override=items.length?null:{key:'noEntries'};
  }
  listSessions(s){
    const items=flattenSessions(s).filter(target=>(!this.harness||target.pluginId===this.harness)&&(this.project===null||projectKey(target)===this.project)).map(target=>({label:target.title??target.nativeSessionId??target.id,target}));
    const selected=items.findIndex(item=>item.target.sessionKey===this.selection?.sessionKey);
    this.list('sessions',items,Math.max(0,selected));
  }
  listProjects(s){
    const items=[...new Map(flattenSessions(s).filter(target=>target.pluginId===this.harness).map(target=>[projectKey(target),{key:projectKey(target),label:target.project??projectKey(target)}])).values()];
    this.list('projects',items,Math.max(0,items.findIndex(it=>it.key===this.project)));
  }
  choose(s,targets){
    const previous=this.selection?.sessionKey;this.selection=targets.find(it=>it.sessionKey===previous)??latestSession(s,targets);
    this.project=this.selection?projectKey(this.selection):null;
    if(previous!==this.selection?.sessionKey){this.model=this.effort=null;this.lastCommand=null;this.contentIndex=0;}
  }
  async action(raw){
    const a=validateAction(raw);
    if(a.locale!==undefined)this.locale=a.locale;
    const cachedRead=['poll','status','browse'].includes(a.op);
    const s=cachedRead&&this.catalogPending&&this.snapshotCache?this.snapshotCache:await this.state();
    if(['poll','status'].includes(a.op))return this.sessionView(s);
    if(a.op==='machines'){this.list('machines',[{label:String(s.hostname??this.machineName),local:true}],0);return this.sessionView(s);}
    if(a.op==='harnesses'){const items=this.harnesses(s);this.list('harnesses',items,Math.max(0,items.findIndex(it=>it.pluginId===this.harness)));return this.sessionView(s);}
    if(a.op==='projects'){this.listProjects(s);return this.sessionView(s);}
    if(a.op==='sessions'){this.listSessions(s);return this.sessionView(s);}
    if(a.op==='browse'){this.menuIndex=Math.max(0,Math.min(a.index??0,this.menu.length-1));return this.sessionView(s);}
    if(a.op==='read'||a.op==='back'){
      ++this.catalogGeneration;this.catalogPending=false;this.menu=[];this.menuKind='';this.override=null;
      if(a.op==='read')this.contentIndex=a.index??this.contentIndex;
      return this.sessionView(s);
    }
    if(a.op==='skin'){this.skin=this.skin==='slate-dark'?'high-contrast':'slate-dark';return this.sessionView(s);}
    if(a.op==='select'){
      const item=this.menu[a.index];if(!item)throw Error('selection_out_of_range');
      ++this.catalogGeneration;this.catalogPending=false;
      if(this.menuKind==='machines'){const items=this.harnesses(s);this.list('harnesses',items,Math.max(0,items.findIndex(it=>it.pluginId===this.harness)));return this.sessionView(s);}
      if(this.menuKind==='harnesses'){
        this.harness=item.pluginId;
        this.choose(s,flattenSessions(s).filter(it=>it.pluginId===this.harness));
        this.persist();this.listProjects(s);return this.sessionView(s);
      }
      if(this.menuKind==='projects'){const key=item.key;this.choose(s,flattenSessions(s).filter(it=>it.pluginId===this.harness&&projectKey(it)===key));this.project=key;this.persist();this.listSessions(s);return this.sessionView(s);}
      if(this.menuKind==='sessions'){this.selection={...item.target};this.harness=this.selection.pluginId;this.project=projectKey(this.selection);this.model=this.effort=null;this.lastCommand=null;this.contentIndex=0;this.persist();}
      else if(this.menuKind==='models'){this.model=item.target.model;this.effort=null;}
      else if(this.menuKind==='efforts')this.effort=item.target;
      this.menu=[];this.menuKind='';this.override=null;return this.sessionView(s);
    }
    if(!this.selection)throw Error('native_session_unavailable');
    if(a.op==='models'||a.op==='efforts'){
      if(this.catalogPending)return this.sessionView(s);
      const targetSession={...this.selection},targetModel=this.model??this.selection.model,generation=++this.catalogGeneration;
      this.menuKind=a.op;this.menu=[];this.menuIndex=0;this.catalogPending=true;this.override={key:'discovering'};
      void (async()=>{
        try{
          const response=await this.api('/v1/harness/models',{pluginId:targetSession.pluginId??targetSession.harness?.pluginId,instanceId:targetSession.harness?.instanceId??'default'});
          if(generation!==this.catalogGeneration||this.selection?.sessionKey!==targetSession.sessionKey)return;
          const models=response.models??[];
          if(a.op==='models')this.menu=models.slice(0,32).map(target=>({label:target.displayName??target.model,target}));
          else this.menu=(models.find(m=>m.model===targetModel)?.efforts??[]).map(target=>({label:typeof target==='string'?target:target.id,target:typeof target==='string'?target:target.id}));
          this.menuIndex=Math.max(0,this.menu.findIndex(it=>a.op==='models'?it.target.model===targetModel:it.target===this.effort));
          this.override=this.menu.length?null:{key:'noCapabilities'};
        }catch(error){if(generation===this.catalogGeneration)this.override={key:'discoveryFailed',suffix:error.message};}
        finally{if(generation===this.catalogGeneration)this.catalogPending=false;}
      })();
      return this.sessionView(s);
    }
    if(a.op==='send'){
      const current=flattenSessions(s).find(it=>it.sessionKey===this.selection.sessionKey);
      if(!current?.ownerId||current.readOnly===true)throw Error('session_not_controllable');
      const commandId='cmd_m5_'+a.commandId;
      if((s.commands??[]).some(c=>c.commandId===commandId)){this.lastCommand=commandId;return this.sessionView(s);}
      this.lastCommand=commandId;this.override=null;
      try{await this.api('/v1/commands',{commandId,sessionKey:current.sessionKey,ownerId:current.ownerId,expectedRevision:current.revision,operation:'sessions.send',payload:{text:a.text,...(this.model?{model:this.model}:{}),...(this.effort?{effort:this.effort}:{})}});}
      catch(error){this.override={key:'deliveryUnknown',suffix:error.message,detail:'inspectStatus'};throw error;}
      return this.sessionView(await this.state());
    }
  }
}
