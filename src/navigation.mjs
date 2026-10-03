import {createHash} from 'node:crypto';
import {flattenSessions,validateAction} from './protocol.mjs';

export const LIST_ROWS=7, CONTENT_ROWS=11;
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
export function sessionText(snapshot,current){
  if(!current)return '';
  const turns=snapshot.turnsStore?.[current.sessionKey]??snapshot.turnsStore?.[current.id]??snapshot.sessionMessages?.[current.sessionKey]??[];
  const messages=[];
  for(const turn of turns){
    const text=turn.text??turn.content;
    if(typeof text==='string'&&text)messages.push(`${String(turn.role??'message').toUpperCase()}\n${text}`);
    if(typeof turn.agentResponse==='string'&&turn.agentResponse&&turn.agentResponse!==text)messages.push(`ASSISTANT\n${turn.agentResponse}`);
  }
  return messages.length?messages.join('\n\n'):current.preview?`PREVIEW\n${current.preview}`:'';
}
export class DeviceNavigation {
  constructor({api,machineName,saved={},save=()=>{}}){
    Object.assign(this,{api,machineName,save,saved,selection:null,harness:saved.harness??null,model:null,effort:null,
      menu:[],menuKind:'',menuIndex:0,lastCommand:null,skin:'slate-dark',override:null,snapshotCache:null,
      catalogPending:false,catalogGeneration:0,contentIndex:0,contentLines:[],contentHash:''});
  }
  async state(){
    const snapshot=await this.api('/v1/snapshot');
    if(snapshot.accessMode!=='local-no-auth')throw Error('middleware_token_mode_not_supported');
    this.snapshotCache=snapshot;
    const sessions=flattenSessions(snapshot);
    if(this.selection){this.selection=sessions.find(s=>s.sessionKey===this.selection.sessionKey)??null;}
    if(!this.initialized){
      this.initialized=true;
      this.selection=sessions.find(s=>s.sessionKey===this.saved.sessionKey)??sessions[0]??null;
      this.harness=this.selection?.pluginId??this.harness;
    }
    return snapshot;
  }
  persist(){this.saved={...this.saved,sessionKey:this.selection?.sessionKey??null,harness:this.harness};this.save(this.saved);}
  harnesses(s){
    return [...new Set([...Object.keys(s.realSessions??{}),...(s.connectedHarnesses??[]).map(h=>h.pluginId),
      ...flattenSessions(s).map(h=>h.pluginId)].filter(Boolean))].map(pluginId=>({label:harnessAbbreviation(pluginId),pluginId}));
  }
  sessionView(s){
    const current=this.selection;
    const text=sessionText(s,current),hash=createHash('sha256').update((current?.sessionKey??'')+'\n'+text).digest('hex');
    if(hash!==this.contentHash){this.contentHash=hash;this.contentLines=text?wrapLines(text):[];}
    this.contentIndex=Math.max(0,Math.min(this.contentIndex,Math.max(0,this.contentLines.length-CONTENT_ROWS)));
    const command=this.lastCommand?(s.commands??[]).find(c=>c.commandId===this.lastCommand):null;
    const offset=windowStart(this.menu.length,this.menuIndex);
    return {connected:true,machine:String(s.hostname??this.machineName),harness:harnessAbbreviation(this.harness??current?.pluginId??''),
      sessionKey:current?.sessionKey??'',session:String(current?.title??current?.nativeSessionId??'세션 선택'),
      destination:String(current?.title??'세션 선택'),model:String(this.model??current?.model??'Native default').slice(0,40),
      effort:String(this.effort??current?.effort??'Native default').slice(0,24),readOnly:current?.readOnly??true,
      commandStatus:command?.status??(this.lastCommand?'unconfirmed':'idle'),message:this.override??(command?`Command: ${command.status}`:'Ready'),
      items:this.menu.slice(offset,offset+LIST_ROWS).map(it=>Array.from(it.label).slice(0,80).join('')),
      menuKind:this.menuKind,menuTotal:this.menu.length,menuOffset:offset,menuIndex:this.menuIndex,
      contentLines:this.contentLines.slice(this.contentIndex,this.contentIndex+CONTENT_ROWS),contentOffset:this.contentIndex,
      contentTotal:this.contentLines.length,contentHash:this.contentHash,skinId:this.skin,skinName:this.skin==='slate-dark'?'Slate Dark':'High Contrast'};
  }
  list(kind,items,index=0){
    ++this.catalogGeneration;this.catalogPending=false;this.menuKind=kind;this.menu=items;
    this.menuIndex=Math.max(0,Math.min(index,items.length-1));this.override=items.length?null:'No native entries';
  }
  listSessions(s){
    const items=flattenSessions(s).filter(target=>!this.harness||target.pluginId===this.harness).map(target=>({label:target.title??target.nativeSessionId??target.id,target}));
    const selected=items.findIndex(item=>item.target.sessionKey===this.selection?.sessionKey);
    this.list('sessions',items,Math.max(0,selected));
  }
  async action(raw){
    const a=validateAction(raw);
    const cachedRead=['poll','status','browse'].includes(a.op);
    const s=cachedRead&&this.catalogPending&&this.snapshotCache?this.snapshotCache:await this.state();
    if(['poll','status'].includes(a.op))return this.sessionView(s);
    if(a.op==='machines'){this.list('machines',[{label:String(s.hostname??this.machineName),local:true}],0);return this.sessionView(s);}
    if(a.op==='harnesses'){const items=this.harnesses(s);this.list('harnesses',items,Math.max(0,items.findIndex(it=>it.pluginId===this.harness)));return this.sessionView(s);}
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
        if(this.selection?.pluginId!==this.harness){this.selection=null;this.model=this.effort=null;this.lastCommand=null;this.contentIndex=0;}
        this.persist();this.listSessions(s);return this.sessionView(s);
      }
      if(this.menuKind==='sessions'){this.selection={...item.target};this.harness=this.selection.pluginId;this.model=this.effort=null;this.lastCommand=null;this.contentIndex=0;this.persist();}
      else if(this.menuKind==='models'){this.model=item.target.model;this.effort=null;}
      else if(this.menuKind==='efforts')this.effort=item.target;
      this.menu=[];this.menuKind='';this.override=null;return this.sessionView(s);
    }
    if(!this.selection)throw Error('select_session_first');
    if(a.op==='models'||a.op==='efforts'){
      if(this.catalogPending)return this.sessionView(s);
      const targetSession={...this.selection},targetModel=this.model??this.selection.model,generation=++this.catalogGeneration;
      this.menuKind=a.op;this.menu=[];this.menuIndex=0;this.catalogPending=true;this.override='Discovering native capabilities...';
      void (async()=>{
        try{
          const response=await this.api('/v1/harness/models',{pluginId:targetSession.pluginId??targetSession.harness?.pluginId,instanceId:targetSession.harness?.instanceId??'default'});
          if(generation!==this.catalogGeneration||this.selection?.sessionKey!==targetSession.sessionKey)return;
          const models=response.models??[];
          if(a.op==='models')this.menu=models.slice(0,32).map(target=>({label:target.displayName??target.model,target}));
          else this.menu=(models.find(m=>m.model===targetModel)?.efforts??[]).map(target=>({label:typeof target==='string'?target:target.id,target:typeof target==='string'?target:target.id}));
          this.menuIndex=Math.max(0,this.menu.findIndex(it=>a.op==='models'?it.target.model===targetModel:it.target===this.effort));
          this.override=this.menu.length?null:'No native capabilities reported';
        }catch(error){if(generation===this.catalogGeneration)this.override=`Native discovery failed: ${error.message}`;}
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
      catch(error){this.override=`Delivery unconfirmed: ${error.message}. Inspect status; do not resend.`;throw error;}
      return this.sessionView(await this.state());
    }
  }
}
