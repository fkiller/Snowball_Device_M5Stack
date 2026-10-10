#pragma once
#include <Arduino.h>
#include <esp_system.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
namespace snowball {
inline bool unhex(const String& text,unsigned char* out,size_t bytes){
  if(text.length()!=bytes*2)return false;
  auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
  for(size_t i=0;i<bytes;i++){int a=digit(text[2*i]),b=digit(text[2*i+1]);if(a<0||b<0)return false;out[i]=(a<<4)|b;}return true;
}
inline String hexBytes(const unsigned char* bytes,size_t length){String out;out.reserve(length*2);for(size_t i=0;i<length;i++){char h[3];snprintf(h,3,"%02x",bytes[i]);out+=h;}return out;}
class LanKeyExchange {
  mbedtls_ecp_keypair key;
  static int random(void*,unsigned char* out,size_t bytes){esp_fill_random(out,bytes);return 0;}
public:
  LanKeyExchange(){mbedtls_ecp_keypair_init(&key);}
  ~LanKeyExchange(){mbedtls_ecp_keypair_free(&key);}
  bool generate(String& publicKey){
    unsigned char bytes[65];size_t size=0;
    if(mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,&key,random,nullptr)||mbedtls_ecp_point_write_binary(&key.grp,&key.Q,MBEDTLS_ECP_PF_UNCOMPRESSED,&size,bytes,sizeof(bytes))||size!=65)return false;
    publicKey=hexBytes(bytes,size);return true;
  }
  bool decrypt(const String& serverPublic,const String& aad,const String& ivHex,const String& ciphertextHex,const String& tagHex,String& enrollmentKey){
    unsigned char point[65],shared[32],aes[32],iv[12],ciphertext[64],tag[16],plain[65]={};
    if(!unhex(serverPublic,point,65)||!unhex(ivHex,iv,12)||!unhex(ciphertextHex,ciphertext,64)||!unhex(tagHex,tag,16))return false;
    mbedtls_ecp_point peer;mbedtls_mpi secret;mbedtls_ecp_point_init(&peer);mbedtls_mpi_init(&secret);
    int status=mbedtls_ecp_point_read_binary(&key.grp,&peer,point,sizeof(point));
    if(!status)status=mbedtls_ecp_check_pubkey(&key.grp,&peer);
    if(!status)status=mbedtls_ecdh_compute_shared(&key.grp,&secret,&peer,&key.d,random,nullptr);
    if(!status)status=mbedtls_mpi_write_binary(&secret,shared,sizeof(shared));
    mbedtls_ecp_point_free(&peer);mbedtls_mpi_free(&secret);
    if(status)return false;
    mbedtls_md_context_t hash;mbedtls_md_init(&hash);
    status=mbedtls_md_setup(&hash,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),0);
    if(!status)status=mbedtls_md_starts(&hash);
    if(!status)status=mbedtls_md_update(&hash,shared,sizeof(shared));
    if(!status)status=mbedtls_md_update(&hash,(const unsigned char*)aad.c_str(),aad.length());
    if(!status)status=mbedtls_md_finish(&hash,aes);
    mbedtls_md_free(&hash);mbedtls_platform_zeroize(shared,sizeof(shared));
    mbedtls_gcm_context gcm;mbedtls_gcm_init(&gcm);
    if(!status)status=mbedtls_gcm_setkey(&gcm,MBEDTLS_CIPHER_ID_AES,aes,256);
    if(!status)status=mbedtls_gcm_auth_decrypt(&gcm,sizeof(ciphertext),iv,sizeof(iv),(const unsigned char*)aad.c_str(),aad.length(),tag,sizeof(tag),ciphertext,plain);
    mbedtls_gcm_free(&gcm);mbedtls_platform_zeroize(aes,sizeof(aes));
    if(!status)enrollmentKey=String((char*)plain);
    mbedtls_platform_zeroize(plain,sizeof(plain));return status==0;
  }
};
}
