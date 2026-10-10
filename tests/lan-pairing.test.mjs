import test from 'node:test';
import assert from 'node:assert/strict';
import {createECDH,createHash,createDecipheriv} from 'node:crypto';
import {LanPairing,pairingMaterial} from '../src/lan-pairing.mjs';
import {sign} from '../src/protocol.mjs';
const hostId='host_'+'a'.repeat(32),epoch='b'.repeat(32),deviceId='m5-001122334455',peer='192.168.1.2',nonce='c'.repeat(16);
function exchange(service){
  const offer=service.offer({nonce,deviceId,peer}),client=createECDH('prime256v1');client.generateKeys();
  const request={hostId,deviceId,nonce,ticket:offer.ticket,publicKey:client.getPublicKey('hex','uncompressed')};
  const reply=service.exchange(request,peer),aad=pairingMaterial(offer,deviceId,request.publicKey);
  const secret=createHash('sha256').update(client.computeSecret(Buffer.from(offer.publicKey,'hex'))).update(aad).digest();
  const cipher=createDecipheriv('aes-256-gcm',secret,Buffer.from(reply.iv,'hex'));cipher.setAAD(Buffer.from(aad));cipher.setAuthTag(Buffer.from(reply.tag,'hex'));
  const key=Buffer.concat([cipher.update(Buffer.from(reply.ciphertext,'hex')),cipher.final()]).toString();
  return {offer,request,reply,aad,key,confirm:{...request,mac:sign(key,'pair-confirm\n'+aad)}};
}
test('LAN enrollment encrypts a distinct key per offer and requires proof before acceptance',()=>{
  const service=new LanPairing({hostId,epoch}),a=exchange(service),b=exchange(service);
  assert.match(a.key,/^[a-f0-9]{64}$/);assert.notEqual(a.key,b.key);
  assert.ok(!JSON.stringify(a.reply).includes(a.key));
  assert.throws(()=>service.confirm({...a.confirm,mac:sign(b.key,'pair-confirm\n'+a.aad)},peer));
  const accepted=service.confirm(a.confirm,peer);assert.equal(accepted.key,a.key);assert.equal(accepted.mac,sign(a.key,'pair-accepted\n'+a.aad));
  assert.deepEqual(service.confirm(a.confirm,peer),accepted,'lost receipt can be read again without rotating the key');
  assert.throws(()=>service.exchange(a.request,peer));
});
test('pairing is bound to peer, board, host, nonce, valid curve point and a short-lived bounded ticket',()=>{
  let now=0;const service=new LanPairing({hostId,epoch,now:()=>now}),a=exchange(service);
  for(const mutation of [{hostId:'host_'+'d'.repeat(32)},{deviceId:'m5-001122334456'},{nonce:'d'.repeat(16)},{ticket:'d'.repeat(32)}])assert.throws(()=>service.confirm({...a.confirm,...mutation},peer));
  assert.throws(()=>service.confirm(a.confirm,'192.168.1.3'));
  const offer=service.offer({nonce,deviceId,peer});assert.throws(()=>service.exchange({hostId,deviceId,nonce,ticket:offer.ticket,publicKey:'04'+'0'.repeat(128)},peer));
  now=15001;assert.throws(()=>service.confirm(a.confirm,peer));
  for(let i=0;i<32;i++)assert.ok(service.offer({nonce,deviceId,peer}));
  assert.equal(service.offer({nonce,deviceId,peer}),null);
  now=30002;assert.ok(service.offer({nonce,deviceId,peer}));
});
