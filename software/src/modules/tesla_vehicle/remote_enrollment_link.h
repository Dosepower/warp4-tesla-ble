#pragma once
#include "remote_enrollment.h"
#include "remote_fragments.h"
#include "remote_enrollment_port.h"
// Single main-thread endpoint; callbacks copy ATT packets only. Fresh CSPRNG
// scalar+nonce each attempt. Staged storage is never an authorization record.
class RemoteEnrollmentLink {
public:
 struct IO {void *context;bool(*random)(void *,uint8_t *,size_t);bool(*save)(void *,const uint8_t peer[32],const uint8_t key[32],unsigned phase);};
 RemoteEnrollmentLink(bool tag,IO io):tag_role(tag),io(io),engine(tag,{this,persist}){}
 RemoteEnrollmentPort port(){return {this,
  [](void*p,uint64_t n,uint8_t a){return static_cast<RemoteEnrollmentLink*>(p)->begin(n,a);},
  [](void*p,uint64_t n,const uint8_t*b,size_t s){return static_cast<RemoteEnrollmentLink*>(p)->receive(n,b,s);},
  [](void*p,uint64_t n,uint8_t*b){return static_cast<RemoteEnrollmentLink*>(p)->frame(n,b);},
  [](void*p,uint64_t n){static_cast<RemoteEnrollmentLink*>(p)->sent(n);},
  [](void*p){return static_cast<RemoteEnrollmentLink*>(p)->complete();},
  [](void*p){return static_cast<RemoteEnrollmentLink*>(p)->failed();},
  [](void*p){static_cast<RemoteEnrollmentLink*>(p)->cancel();}};}
 bool begin(uint64_t now,uint8_t attempt){
  if(active||!attempt||now>UINT64_MAX-30000)return false;
  engine.reset();
  rx.reset();done=bad=derived=consented=final_saved=hello_confirmed=false;active=true;cookie=attempt;until=now+30000;next_retry=now;tries=0;tx_size=tx_at=0;tx_type=0;
  uint8_t scalar[32]{},nonce[32]{};bool ok=false;
  for(unsigned i=0;i<8&&!ok;i++){
   if(!io.random(io.context,scalar,32)||!io.random(io.context,nonce,32))break;
   ok=engine.begin(scalar,nonce);
  }
  wipe(scalar,32);wipe(nonce,32);if(!ok)return fail();
  if(!tag_role)queue(1,engine.hello(),96,now);
  return true;
 }
 bool receive(uint64_t now,const uint8_t *frame,size_t n){
  poll(now);if(!active||bad||n<7||!frame)return false;
  if(rx.complete()&&frame[2]!=rx.type()){
   if(frame[3]!=0)return fail();rx.reset();
  }
  auto result=rx.feed(frame,n,cookie);
  if(result==RemoteFragments::Invalid)return fail();
  if(result==RemoteFragments::Duplicate){
   if(rx.complete()&&frame[3]+n-6==rx.size()&&tx_size&&tx_at==tx_size){tx_at=0;next_retry=now+500;}
   return true;
  }
  if(result!=RemoteFragments::Complete)return true;
  if(rx.type()==1){
   if(derived)return fail();
   if(!engine.peer(rx.data()))return fail();
   if(!engine.binding_id(peer_id))return fail();
   derived=true;
   if(tag_role)queue(1,engine.hello(),96,now);
   else{uint8_t receipt[17];if(!engine.hello_receipt(receipt))return fail();queue(9,receipt,17,now);wipe(receipt,17);}
   return true;
  }
  if(rx.type()==9){
   if(!tag_role||hello_confirmed||!engine.verify_hello_receipt(rx.data()))return fail();
   hello_confirmed=true;tx_size=tx_at=0;tx_type=0;tries=0;return true;
  }
  if(!derived||!engine.accept(rx.data()))return fail();
  tx_size=tx_at=0;tx_type=0;tries=0;
  if(!queue_proof(now))return false;
  if(engine.complete())return finish();
  return true;
 }
 bool awaiting_consent()const{return active&&derived&&hello_confirmed&&tag_role&&!consented&&!bad;}
 bool consent(uint64_t now){
  poll(now);if(!awaiting_consent()||!engine.consent())return false;
  consented=true;return queue_proof(now);
 }
 size_t frame(uint64_t now,uint8_t out[20]){
  poll(now);if(!active||bad||!tx_size||tx_at>=tx_size)return 0;
  return RemoteFragments::encode(cookie,tx_type,tx,tx_size,tx_at,out);
 }
 void sent(uint64_t now){
  if(!tx_size||tx_at>=tx_size)return;
  size_t n=tx_size-tx_at;if(n>14)n=14;tx_at+=n;
  if(tx_at==tx_size){next_retry=now+500;if(!tag_role&&engine.complete())(void)finish();}
 }
 void poll(uint64_t now){
  if(!active||bad)return;
  if(now>=until){fail();return;}
  if((!done||tag_role)&&tx_size&&tx_at==tx_size&&now>=next_retry){
   if(++tries>60){fail();return;}tx_at=0;next_retry=now+500;
  }
 }
 bool complete()const{return done&&!bad&&(!tag_role||tx_at==tx_size);}
 bool failed()const{return bad;}
 const uint8_t *identity()const{return peer_id;}
 void cancel(){fail();}
private:
 bool tag_role;IO io;RemoteEnrollment engine;RemoteFragments rx;
 bool active=false,done=false,bad=false,derived=false,consented=false,final_saved=false,hello_confirmed=false;
 uint8_t cookie=0,tx_type=0,tx[96]{},peer_id[32]{},prepared_key[32]{};
 size_t tx_size=0,tx_at=0;uint64_t until=0,next_retry=0;unsigned tries=0;
 static void wipe(void *p,size_t n){volatile uint8_t *v=static_cast<volatile uint8_t *>(p);while(n--)*v++=0;}
 static bool persist(void *ctx,const uint8_t *key,bool committed){
  auto &s=*static_cast<RemoteEnrollmentLink *>(ctx);
  if(!s.io.save(s.io.context,s.peer_id,key,committed?2:1))return false;
  memcpy(s.prepared_key,key,32);return true;
 }
 bool finish(){
  if(!final_saved&&!io.save(io.context,peer_id,prepared_key,3))return fail();
  final_saved=true;done=true;wipe(prepared_key,32);return true;
 }
 void queue(uint8_t type,const uint8_t *p,size_t n,uint64_t now){tx_type=type;memcpy(tx,p,n);tx_size=n;tx_at=0;next_retry=now+500;tries=0;}
 bool queue_proof(uint64_t now){
  uint8_t kind=engine.pending_kind(),proof[17]{};
  if(!kind)return true;
  if(!engine.next(proof))return fail();
  queue(kind+1,proof,17,now);wipe(proof,sizeof(proof));return true;
 }
 bool fail(){engine.cancel();rx.reset();wipe(tx,96);wipe(prepared_key,32);tx_at=tx_size=0;bad=true;done=false;active=false;return false;}
};
