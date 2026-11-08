#pragma once
#include "remote_crypto.h"
#include "remote_fragments.h"
#include "remote_enrollment_port.h"
#include <string.h>
// One bounded transaction. No charging opcode admitted. Normal Tag request
// starts with its stored binding id; it is a lookup hint, never authentication.
class RemoteSessionLink {
public:
 // respond starts at most once, only after the request AEAD gate. Optional
 // pending/finish/abort permit main-task polling without rerunning admission.
 // finish: 0 waiting, 1 immutable correlated result, -1 failure. cancel/timeout
 // abort one pending operation exactly once; they cannot undo a future writer.
 struct IO {void*context;bool(*random)(void*,uint8_t*,size_t);bool(*load)(void*,const uint8_t*,uint8_t*,uint8_t*);bool(*reserve)(void*,const uint8_t*,uint64_t*);bool(*respond)(void*,const uint8_t[16],uint8_t[64]);void(*deliver)(void*,const uint8_t[64]);bool(*pending)(void*)=nullptr;int(*finish)(void*,const uint8_t[16],uint8_t[64])=nullptr;void(*abort)(void*)=nullptr;bool(*accepted)(void*)=nullptr;};
 RemoteSessionLink(bool tag,IO io):tag(tag),io(io){}
 ~RemoteSessionLink(){cancel();}
 bool request(uint8_t op,uint64_t operation=0){if(active||op<1||op>3||(operation&&op!=3))return false;opcode=op;operation_id=operation;return true;}
 RemoteEnrollmentPort port(){auto p=RemoteEnrollmentPort{this,
  [](void*p,uint64_t n,uint8_t a){return ((RemoteSessionLink*)p)->begin(n,a);},
  [](void*p,uint64_t n,const uint8_t*b,size_t s){return ((RemoteSessionLink*)p)->receive(n,b,s);},
  [](void*p,uint64_t n,uint8_t*b){return ((RemoteSessionLink*)p)->frame(n,b);},
  [](void*p,uint64_t n){((RemoteSessionLink*)p)->sent(n);},
  [](void*p){return ((RemoteSessionLink*)p)->complete();},
  [](void*p){return ((RemoteSessionLink*)p)->failed();},
  [](void*p){((RemoteSessionLink*)p)->cancel();}};p.selection=4;p.normal_session=true;return p;}
 bool begin(uint64_t now,uint8_t attempt){
  if(active||!attempt||now>UINT64_MAX-10000)return false;cancel();active=true;bad=false;cookie=attempt;until=now+10000;stage=tag?1:0;
  if(tag){if(!initialize(nullptr))return fail();queue(17,local,96,now);}return true;
 }
 bool receive(uint64_t now,const uint8_t*p,size_t n){
  poll(now);if(!active||bad||!p||n<7)return false;
  if(rx.complete()&&p[2]!=rx.type()){if(p[3])return fail();rx.reset();}
  auto r=rx.feed(p,n,cookie);if(r==RemoteFragments::Invalid)return fail();
  if(r==RemoteFragments::Duplicate){if(rx.complete()&&p[3]+n-6==rx.size()&&tx_size&&tx_at==tx_size){tx_at=0;retry=now+500;}return true;}
  if(r!=RemoteFragments::Complete)return true;
  const auto*b=rx.data();uint8_t type=rx.type();
  if(type==17){
   if(derived||stage!=(tag?1:0)||b[72]!=(tag?0:1)||!nonzero(b+32,32)||!nonzero(b+64,8)||nonzero(b+73,23))return fail();
   if(!tag&&!initialize(b))return fail();
   if(memcmp(id,b,32))return fail();memcpy(peer,b,96);uint8_t salt[192],keys[64];
   memcpy(salt,tag?peer:local,96);memcpy(salt+96,tag?local:peer,96);
   static const uint8_t domain[]="WARP operational directional keys v1";
   bool ok=wr_crypto_hkdf(keys,64,root,32,salt,192,domain,sizeof(domain)-1);wipe(root,32);wipe(salt,192);
   if(ok){memcpy(send_key,keys+(tag?32:0),32);memcpy(recv_key,keys+(tag?0:32),32);}wipe(keys,64);if(!ok)return fail();
   derived=true;
   if(!tag){stage=2;queue(17,local,96,now);return true;}
   stage=3;return seal(18,1,1,&one,1,now);
  }
  if(!derived)return fail();
  uint8_t plain[64]{};
  if(!tag&&type==18&&stage==2){if(!open(1,1,b,1,plain)||plain[0]!=1)return fail();stage=4;return seal(19,2,0,&two,1,now);}
  if(tag&&type==19&&stage==3){
   if(!open(2,0,b,1,plain)||plain[0]!=2)return fail();request_bytes[0]=opcode;memcpy(request_bytes+1,local+64,8);
   if(operation_id)for(unsigned i=0;i<8;i++)request_bytes[1+i]=(uint8_t)(operation_id>>(56-8*i));stage=5;
   return seal(20,3,1,request_bytes,16,now);
  }
  if(!tag&&type==20&&stage==4){
   if(!open(3,1,b,16,plain)||plain[0]<1||plain[0]>3||nonzero(plain+9,7))return fail();
   if(plain[0]==3){if(!nonzero(plain+1,8)||memcmp(plain+1,peer+64,8)>0)return fail();}
   else if(memcmp(plain+1,peer+64,8))return fail();
   uint8_t reply[64]{};memcpy(reply,plain,16);
   if(!io.respond(io.context,plain,reply)){
    if(!io.pending||!io.finish||!io.abort||!io.pending(io.context)){wipe(reply,64);return fail();}
    memcpy(request_bytes,plain,16);waiting=true;stage=9;wipe(tx,96);tx_size=tx_at=0;wipe(reply,64);return true;
   }
   if(memcmp(reply,plain,16)){wipe(reply,64);return fail();}
   stage=6;bool ok=seal(21,4,0,reply,64,now);wipe(reply,64);return ok;
  }
  if(tag&&type==21&&stage==5){
   if(!open(4,0,b,64,plain)||memcmp(plain,request_bytes,16))return fail();
   io.deliver(io.context,plain);if(io.accepted&&!io.accepted(io.context))return fail();stage=7;done=true;return seal(22,5,1,&three,1,now);
  }
  if(!tag&&type==22&&stage==6){if(!open(5,1,b,1,plain)||plain[0]!=3)return fail();tx_size=tx_at=0;done=true;stage=8;return true;}
  return fail();
 }
 size_t frame(uint64_t now,uint8_t out[20]){poll(now);if(!active||bad||tx_at>=tx_size)return 0;return RemoteFragments::encode(cookie,tx_type,tx,tx_size,tx_at,out);}
 void sent(uint64_t now){if(tx_at>=tx_size)return;size_t n=tx_size-tx_at;if(n>14)n=14;tx_at+=n;if(tx_at==tx_size)retry=now+500;}
 void poll(uint64_t now){
  if(!active||bad)return;if(now>=until){fail();return;}
  if(waiting){
   uint8_t reply[64]{};memcpy(reply,request_bytes,16);const int ready=io.finish(io.context,request_bytes,reply);
   if(ready<0||ready>1||(ready==1&&memcmp(reply,request_bytes,16))){wipe(reply,64);fail();return;}
   if(ready==1){waiting=false;stage=6;seal(21,4,0,reply,64,now);}
   wipe(reply,64);
  }
  if(tx_size&&tx_at==tx_size&&now>=retry){tx_at=0;retry=now+500;}
 }
 bool complete()const{return active&&!bad&&done&&(!tag||tx_at==tx_size);}
 bool failed()const{return bad;}
 const uint8_t*identity()const{return id;}
 void cancel(){if(waiting){waiting=false;if(io.abort)io.abort(io.context);}wipe(root,32);wipe(send_key,32);wipe(recv_key,32);wipe(local,96);wipe(peer,96);wipe(tx,96);wipe(request_bytes,16);rx.reset();active=derived=done=false;bad=true;tx_size=tx_at=0;}
private:
 bool tag;IO io;bool active=false,bad=true,derived=false,done=false,waiting=false;RemoteFragments rx;
 uint8_t root[32]{},send_key[32]{},recv_key[32]{},id[32]{},local[96]{},peer[96]{},tx[96]{},request_bytes[16]{},cookie=0,tx_type=0,opcode=1,stage=0;
 const uint8_t one=1,two=2,three=3;size_t tx_size=0,tx_at=0;uint64_t until=0,retry=0,operation_id=0;
 static void wipe(void*p,size_t n){volatile uint8_t*v=(volatile uint8_t*)p;while(n--)*v++=0;}
 static bool nonzero(const uint8_t*p,size_t n){uint8_t a=0;while(n--)a|=*p++;return a!=0;}
 bool initialize(const uint8_t*hint){
  uint64_t counter=0;if(!io.load(io.context,hint,id,root)||!io.reserve(io.context,id,&counter)||!counter||(tag&&operation_id>counter))return false;
  memcpy(local,id,32);if(!io.random(io.context,local+32,32)||!nonzero(local+32,32))return false;
  for(unsigned i=0;i<8;i++)local[64+i]=(uint8_t)(counter>>(56-8*i));local[72]=tag?1:0;return true;
 }
 void queue(uint8_t type,const uint8_t*p,size_t n,uint64_t now){memcpy(tx,p,n);tx_type=type;tx_size=n;tx_at=0;retry=now+500;}
 static void fields(uint8_t nonce[13],uint8_t aad[5],uint8_t kind,uint8_t direction){memset(nonce,0,13);nonce[0]=0x53;nonce[1]=direction;nonce[12]=kind;aad[0]=0x57;aad[1]=0x52;aad[2]=3;aad[3]=direction;aad[4]=kind;}
 bool seal(uint8_t type,uint8_t kind,uint8_t direction,const uint8_t*p,size_t n,uint64_t now){uint8_t out[96]{},nonce[13],aad[5];fields(nonce,aad,kind,direction);if(!wr_crypto_seal(out,out+n,send_key,nonce,aad,5,p,n))return fail();queue(type,out,n+16,now);wipe(out,96);return true;}
 bool open(uint8_t kind,uint8_t direction,const uint8_t*p,size_t n,uint8_t*out){uint8_t nonce[13],aad[5];fields(nonce,aad,kind,direction);return wr_crypto_open(out,recv_key,nonce,aad,5,p,n,p+n);}
 bool fail(){cancel();return false;}
};
