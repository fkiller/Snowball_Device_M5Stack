import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
export async function manifest() {
  return {id:'snowball.device-m5stack',publisher:'Snowball',version:'0.2.1',kind:'hardware',sdkApiRange:'^1.0.0',entrypoint:'src/worker.mjs',platforms:['win32','darwin','linux'],architectures:['x64','arm64'],capabilities:[{operation:'devices.list',access:'observe'},{operation:'devices.render',access:'control'}],permissions:[],configSchema:{type:'object',properties:{},additionalProperties:false},integrity:{entrySha256:createHash('sha256').update(await readFile(new URL('./worker.mjs',import.meta.url))).digest('hex')},license:'Apache-2.0'};
}
