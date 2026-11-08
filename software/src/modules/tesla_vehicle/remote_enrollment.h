#pragma once
#include "remote_crypto.h"
#include <string.h>
// Application enrollment state machine; P-256/HKDF/CCM are reviewed primitives,
// but this protocol still needs independent review + transport/recovery tests.
// Initial blink/click consent is TOFU, NOT strong initial MITM authentication.
class RemoteEnrollment {
public:
 struct Persistence {void *context;bool(*verified_save)(void *,const uint8_t key[32],bool committed);};
 RemoteEnrollment(bool tag,Persistence store):tag_role(tag),store(store){}
 ~RemoteEnrollment(){clear();}
 bool begin(const uint8_t scalar[32],const uint8_t nonce[32]){
  if(started||!scalar||!nonce)return false;
  uint8_t any=0;for(unsigned i=0;i<32;i++)any|=nonce[i];if(!any)return false;
  if(!wr_crypto_public(local,scalar))return false;
  memcpy(secret,scalar,32);memcpy(local+64,nonce,32);started=true;return true;
 }
 const uint8_t *hello()const{return local;}
 bool peer(const uint8_t hello[96]){
  if(!started||derived||failed||!hello)return false;
  uint8_t any=0;for(unsigned i=64;i<96;i++)any|=hello[i];if(!any)return reject();
  uint8_t shared[32],salt[64],info[144]{};
  memcpy(info,"WARP remote v1",14);
  const uint8_t *warp=tag_role?hello:local,*tag=tag_role?local:hello;
  memcpy(salt,warp+64,32);memcpy(salt+32,tag+64,32);
  memcpy(info+16,warp,64);memcpy(info+80,tag,64);
  bool ok=wr_crypto_shared(shared,secret,hello)&&wr_crypto_hkdf(root,32,shared,32,salt,64,info,sizeof(info));
  zero(shared,sizeof(shared));zero(secret,sizeof(secret));zero(salt,sizeof(salt));zero(info,sizeof(info));
  if(!ok)return reject();
  derived=true;expected=tag_role?2:1;return true;
 }
 bool binding_id(uint8_t out[32])const{
  static const uint8_t info[]="WARP remote binding id v1";
  return derived&&!failed&&out&&wr_crypto_hkdf(out,32,root,32,nullptr,0,info,sizeof(info)-1);
 }
 bool hello_receipt(uint8_t out[17]){
  if(tag_role||!derived||failed||consented||done||!out)return false;
  uint8_t kind=0,nonce[13]{},aad[4]={0x57,0x52,1,0};nonce[0]=0x45;
  return wr_crypto_seal(out,out+1,root,nonce,aad,4,&kind,1);
 }
 bool verify_hello_receipt(const uint8_t in[17]){
  if(!tag_role||!derived||failed||consented||done||!in)return false;
  uint8_t kind=1,nonce[13]{},aad[4]={0x57,0x52,1,0};nonce[0]=0x45;
  return wr_crypto_open(&kind,root,nonce,aad,4,in,1,in+1)&&kind==0;
 }
 bool consent(){
  if(!tag_role||!derived||failed||send||consented)return false;
  consented=true;send=1;return true;
 }
 bool next(uint8_t out[17]){
  if(!out||failed||!send)return false;
  uint8_t kind=send,nonce[13]{},aad[4]={0x57,0x52,1,kind};nonce[0]=0x45;nonce[12]=kind;
  if(!wr_crypto_seal(out,out+1,root,nonce,aad,sizeof(aad),&kind,1))return reject();
  send=0;if(kind==7)done=true;return true;
 }
 bool accept(const uint8_t proof[17]){
  if(!proof||!derived||failed||done||send||(tag_role&&!consented))return false;
  uint8_t kind=expected,plain=0,nonce[13]{},aad[4]={0x57,0x52,1,kind};nonce[0]=0x45;nonce[12]=kind;
  if(!wr_crypto_open(&plain,root,nonce,aad,sizeof(aad),proof,1,proof+1)||plain!=kind)return reject();
  if((tag_role&&kind==2)||(!tag_role&&kind==1)){
   if(!store.verified_save(store.context,root,false))return reject();
   send=kind+1;expected=kind+2;return true;
  }
  if((tag_role&&kind==4)||(!tag_role&&kind==3)){
   if(!store.verified_save(store.context,root,true))return reject();
   send=kind+1;expected=kind+2;return true;
  }
  if(!tag_role&&kind==5){send=6;expected=7;return true;}
  if(tag_role&&kind==6){send=7;expected=8;return true;}
  if(!tag_role&&kind==7){done=true;return true;}
  return reject();
 }
 uint8_t pending_kind()const{return send;}
 bool faulted()const{return failed;}
 bool complete()const{return done&&!failed;}
 void cancel(){reject();}
 void reset(){clear();zero(local,sizeof(local));started=derived=failed=done=consented=false;expected=send=0;}
private:
 bool tag_role;Persistence store;
 bool started=false,derived=false,failed=false,done=false,consented=false;
 uint8_t expected=0,send=0,secret[32]{},root[32]{},local[96]{};
 static void zero(void *ptr,size_t n){volatile uint8_t *p=static_cast<volatile uint8_t *>(ptr);while(n--)*p++=0;}
 void clear(){zero(secret,sizeof(secret));zero(root,sizeof(root));zero(local,sizeof(local));}
 bool reject(){failed=true;done=false;send=0;clear();return false;}
};
