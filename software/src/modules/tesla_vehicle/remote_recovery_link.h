#pragma once
#include "remote_crypto.h"
#include "remote_fragments.h"
#include "remote_enrollment_port.h"
#include <string.h>
// Explicit recovery of the SAME persisted binding, never new enrollment.
// Fresh random hellos and domain-separated proof key fence old final receipts.
// Physical confirm is required again using the existing setup/confirm UX.
class RemoteRecoveryLink {
public:
 struct IO {void *context;bool(*random)(void*,uint8_t*,size_t);bool(*load)(void*,const uint8_t*,uint8_t*,uint8_t*,unsigned*);bool(*save)(void*,unsigned);};
 RemoteRecoveryLink(bool tag,IO io):tag(tag),io(io){}
 ~RemoteRecoveryLink(){cancel();}
 bool select(const uint8_t *id){if(active||!id)return false;memcpy(selected,id,32);selected_valid=true;return true;}
 RemoteEnrollmentPort port(){return {this,
  [](void*p,uint64_t n,uint8_t a){return ((RemoteRecoveryLink*)p)->begin(n,a);},
  [](void*p,uint64_t n,const uint8_t*b,size_t s){return ((RemoteRecoveryLink*)p)->receive(n,b,s);},
  [](void*p,uint64_t n,uint8_t*b){return ((RemoteRecoveryLink*)p)->frame(n,b);},
  [](void*p,uint64_t n){((RemoteRecoveryLink*)p)->sent(n);},
  [](void*p){return ((RemoteRecoveryLink*)p)->complete();},
  [](void*p){return ((RemoteRecoveryLink*)p)->failed();},
  [](void*p){((RemoteRecoveryLink*)p)->cancel();}};}
 bool begin(uint64_t now,uint8_t attempt){
  if(active||!attempt||now>UINT64_MAX-30000||(!tag&&!selected_valid))return false;
  cancel();bad=false;active=true;cookie=attempt;until=now+30000;unsigned phase=0;
  if(!io.load(io.context,tag?nullptr:selected,id,root,&phase)||phase<1||phase>3)return fail();
  memcpy(local,id,32);local[64]=tag?1:0;local[65]=(uint8_t)phase;
  if(!io.random(io.context,local+32,32)||!nonzero(local+32,32))return fail();
  if(!tag)queue(10,local,96,now);return true;
 }
 bool receive(uint64_t now,const uint8_t *p,size_t n){
  poll(now);if(!active||bad||!p||n<7)return false;
  if(rx.complete()&&p[2]!=rx.type()){if(p[3])return fail();rx.reset();}
  auto r=rx.feed(p,n,cookie);if(r==RemoteFragments::Invalid)return fail();
  if(r==RemoteFragments::Duplicate){if(rx.complete()&&p[3]+n-6==rx.size()&&tx_size&&tx_at==tx_size){tx_at=0;retry=now+500;}return true;}
  if(r!=RemoteFragments::Complete)return true;
  const uint8_t *b=rx.data();uint8_t type=rx.type();
  if(type==10){
   if(derived||memcmp(b,id,32)||b[64]!=(tag?0:1)||b[65]<1||b[65]>3||!nonzero(b+32,32)||nonzero(b+66,30))return fail();
   memcpy(peer,b,96);uint8_t salt[192];memcpy(salt,tag?peer:local,96);memcpy(salt+96,tag?local:peer,96);
   static const uint8_t domain[]="WARP recovery proofs v1";
   bool ok=wr_crypto_hkdf(key,32,root,32,salt,192,domain,sizeof(domain)-1);wipe(salt,192);wipe(root,32);
   if(!ok)return fail();derived=true;
   if(tag)queue(10,local,96,now);else return proof(0,now);return true;
  }
  if(!derived||type<11||type>16)return fail();
  uint8_t kind=type-11,plain=255,nonce[13]{},aad[4]={0x57,0x52,2,kind};nonce[0]=0x52;nonce[12]=kind;
  if(kind!=expected||!wr_crypto_open(&plain,key,nonce,aad,4,b,1,b+1)||plain!=kind)return fail();
  tx_at=tx_size=0;
  if(tag&&kind==0){ready=true;expected=2;return true;}
  if(!tag&&kind==1){if(!io.save(io.context,2))return fail();expected=3;return proof(2,now);}
  if(tag&&kind==2&&consented){if(!io.save(io.context,2))return fail();expected=4;return proof(3,now);}
  if(!tag&&kind==3){expected=5;return proof(4,now);}
  if(tag&&kind==4&&consented){if(!io.save(io.context,3))return fail();done=true;return proof(5,now);}
  if(!tag&&kind==5){if(!io.save(io.context,3))return fail();done=true;return true;}
  return fail();
 }
 bool awaiting_consent()const{return active&&!bad&&tag&&ready&&!consented;}
 bool consent(uint64_t now){poll(now);if(!awaiting_consent())return false;consented=true;return proof(1,now);}
 size_t frame(uint64_t now,uint8_t out[20]){poll(now);if(!active||bad||tx_at>=tx_size)return 0;return RemoteFragments::encode(cookie,tx_type,tx,tx_size,tx_at,out);}
 void sent(uint64_t now){if(tx_at>=tx_size)return;size_t n=tx_size-tx_at;if(n>14)n=14;tx_at+=n;if(tx_at==tx_size)retry=now+500;}
 void poll(uint64_t now){if(!active||bad)return;if(now>=until){fail();return;}if(tx_size&&tx_at==tx_size&&now>=retry){tx_at=0;retry=now+500;}}
 bool complete()const{return active&&!bad&&done&&(!tag||tx_at==tx_size);}
 bool failed()const{return bad;}
 const uint8_t *identity()const{return id;}
 void cancel(){wipe(root,32);wipe(key,32);wipe(local,96);wipe(peer,96);wipe(tx,96);rx.reset();active=derived=ready=consented=done=false;bad=true;tx_at=tx_size=0;expected=tag?0:1;}
private:
 bool tag;IO io;RemoteFragments rx;bool selected_valid=false,active=false,bad=true,derived=false,ready=false,consented=false,done=false;
 uint8_t selected[32]{},id[32]{},root[32]{},key[32]{},local[96]{},peer[96]{},tx[96]{},cookie=0,tx_type=0,expected=0;
 size_t tx_size=0,tx_at=0;uint64_t until=0,retry=0;
 static bool nonzero(const uint8_t*p,size_t n){uint8_t a=0;while(n--)a|=*p++;return a!=0;}
 static void wipe(void*p,size_t n){volatile uint8_t*v=(volatile uint8_t*)p;while(n--)*v++=0;}
 bool fail(){cancel();return false;}
 void queue(uint8_t type,const uint8_t*p,size_t n,uint64_t now){memcpy(tx,p,n);tx_type=type;tx_at=0;tx_size=n;retry=now+500;}
 bool proof(uint8_t kind,uint64_t now){uint8_t out[17],nonce[13]{},aad[4]={0x57,0x52,2,kind};nonce[0]=0x52;nonce[12]=kind;
  if(!wr_crypto_seal(out,out+1,key,nonce,aad,4,&kind,1))return fail();queue(11+kind,out,17,now);wipe(out,17);return true;}
};
