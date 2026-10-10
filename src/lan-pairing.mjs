import {createECDH,createHash,createCipheriv,randomBytes} from 'node:crypto';
import {sign,equal} from './protocol.mjs';

export const pairingMaterial=(offer,deviceId,clientPublic)=>`snowball.pair.v3\n${offer.hostId}\n${deviceId}\n${offer.nonce}\n${offer.ticket}\n${offer.epoch}\n${offer.publicKey}\n${clientPublic}`;
// TOFU on an explicitly selected, trusted-LAN PC. Encrypt a fresh enrollment key
// in transit; never return it again after enrollment, even for a claimed board ID.
export class LanPairing {
  constructor({hostId,epoch,now=Date.now}) {
    Object.assign(this,{hostId,epoch,now});this.ecdh=createECDH('prime256v1');this.ecdh.generateKeys();this.tickets=new Map();
  }
  prune(){for(const [ticket,p]of this.tickets)if(this.now()-p.created>15000)this.tickets.delete(ticket);}
  offer({nonce,deviceId,peer}) {
    this.prune();if(this.tickets.size>=32)return null;
    if(!/^[a-f0-9]{16}$/.test(nonce)||!/^m5-[a-f0-9]{12}$/.test(deviceId))return null;
    const offer={hostId:this.hostId,epoch:this.epoch,nonce,ticket:randomBytes(16).toString('hex'),publicKey:this.ecdh.getPublicKey('hex','uncompressed')};
    this.tickets.set(offer.ticket,{offer,peer,deviceId,created:this.now(),issued:false,key:randomBytes(32).toString('hex')});return offer;
  }
  entry(request,peer){
    this.prune();const p=this.tickets.get(request.ticket);
    if(!p||p.peer!==peer||p.deviceId!==request.deviceId||request.hostId!==this.hostId||request.nonce!==p.offer.nonce)throw Error('pairing_expired_or_mismatched');
    return p;
  }
  exchange(request,peer){
    const p=this.entry(request,peer);
    if(p.issued||!/^04[a-f0-9]{128}$/.test(request.publicKey??''))throw Error('invalid_pairing_request');
    const shared=this.ecdh.computeSecret(Buffer.from(request.publicKey,'hex'));
    const aad=pairingMaterial(p.offer,p.deviceId,request.publicKey),secret=createHash('sha256').update(shared).update(aad).digest(),iv=randomBytes(12);
    const cipher=createCipheriv('aes-256-gcm',secret,iv);cipher.setAAD(Buffer.from(aad));
    const ciphertext=Buffer.concat([cipher.update(p.key,'utf8'),cipher.final()]);
    p.issued=true;p.aad=aad;
    return {type:'snowball.pair',version:3,hostId:this.hostId,deviceId:p.deviceId,nonce:p.offer.nonce,ticket:p.offer.ticket,epoch:this.epoch,iv:iv.toString('hex'),ciphertext:ciphertext.toString('hex'),tag:cipher.getAuthTag().toString('hex')};
  }
  confirm(request,peer){
    const p=this.entry(request,peer);
    if(!p.issued||!equal(request.mac??'',sign(p.key,'pair-confirm\n'+p.aad)))throw Error('invalid_pairing_confirmation');
    p.confirmed=true;
    return {deviceId:p.deviceId,key:p.key,mac:sign(p.key,'pair-accepted\n'+p.aad)};
  }
}
