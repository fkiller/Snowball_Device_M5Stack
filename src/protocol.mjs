import {createHmac, timingSafeEqual} from 'node:crypto';
import {isIPv4} from 'node:net';
export const MAX_FRAME = 16384;
export const privateIp = ip => isIPv4(ip) && (ip.startsWith('127.') || ip.startsWith('10.') || ip.startsWith('192.168.') || (ip.startsWith('172.') && +ip.split('.')[1]>=16 && +ip.split('.')[1]<=31));
export const sign = (key, value) => createHmac('sha256',key).update(value).digest('hex');
export function equal(a,b) { return typeof a==='string' && typeof b==='string' && a.length===b.length && timingSafeEqual(Buffer.from(a),Buffer.from(b)); }
export const material = (direction,epoch,boot,seq,payload) => `${direction}\n${epoch}\n${boot}\n${seq}\n${payload}`;
export class ReplayGuard {
  #seen=new Map();
  accept(key,epoch,frame) {
    if(!frame || frame.epoch!==epoch || !/^[a-f0-9]{16}$/.test(frame.boot) || !Number.isInteger(frame.seq) || frame.seq<1 || frame.seq>0xffffffff || typeof frame.payload!=='string' || Buffer.byteLength(frame.payload)>MAX_FRAME) throw Error('invalid_frame');
    if(!equal(frame.mac,sign(key,material('request',epoch,frame.boot,frame.seq,frame.payload)))) throw Error('authentication_failed');
    if(frame.seq <= (this.#seen.get(frame.boot)??0)) throw Error('replay_denied');
    if(!this.#seen.has(frame.boot) && this.#seen.size>=128) throw Error('boot_limit_restart_gateway');
    this.#seen.set(frame.boot,frame.seq);
    return JSON.parse(frame.payload);
  }
}
export function flattenSessions(snapshot) {
  const journal=new Map((snapshot.sessions??[]).map(s=>[s.sessionKey,s]));
  const native=snapshot.realSessions ? Object.entries(snapshot.realSessions).flatMap(([pluginId,projects])=>Object.entries(projects).flatMap(([project,items])=>items.map(s=>({...s,pluginId,project})))) : snapshot.sessionDetails??[];
  return native.filter(s=>typeof s.sessionKey==='string').map(s=>({...s,...journal.get(s.sessionKey),label:`${s.pluginId??''} / ${s.project??''} / ${s.title??s.nativeSessionId??s.id??''}`}));
}
export function validateAction(action) {
  if(!action || typeof action!=='object' || Array.isArray(action)) throw Error('invalid_action');
  if(!['poll','machines','harnesses','sessions','browse','back','models','efforts','select','send','status','read','skin'].includes(action.op)) throw Error('unsupported_action');
  const allowed=['op','index','text','commandId'];
  if(Object.keys(action).some(k=>!allowed.includes(k))) throw Error('invalid_action');
  if(action.index!==undefined && (!Number.isInteger(action.index)||action.index<0||action.index>100000)) throw Error('invalid_index');
  if(action.op==='send' && (typeof action.text!=='string'||!action.text.trim()||Buffer.byteLength(action.text)>2048|| !/^[a-f0-9]{32}$/.test(action.commandId))) throw Error('invalid_prompt');
  return action;
}
