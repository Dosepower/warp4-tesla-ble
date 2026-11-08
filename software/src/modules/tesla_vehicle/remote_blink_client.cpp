#if defined(TESLA_BLE_USE_NIMBLE) && TESLA_BLE_USE_NIMBLE
#include "remote_blink_client.h"
#include <cstring>
#include <limits>
std::atomic<RemoteBlinkClient *> RemoteBlinkClient::owner{nullptr};
bool RemoteBlinkClient::begin(TeslaNimbleCentral &c,uint64_t now,const uint8_t address[6],uint8_t type,const uint8_t nonce[16]){
 if(active()||!address||!nonce||epoch==std::numeric_limits<uint64_t>::max()||now>std::numeric_limits<uint64_t>::max()-30000)return false;
 RemoteBlinkClient *expected=nullptr;
 if(!owner.compare_exchange_strong(expected,this))return false;
 central=&c;++epoch;until=now+30000;received=cancelled=ok=trusted=secure_started=secure_writing=false;
 head=tail=0;overflow=false;memcpy(token,nonce,16);state=Opening;
 if(!c.open_remote(address,type,epoch,callback)){
  state=Failed;memset(token,0,sizeof(token));owner=nullptr;central=nullptr;return false;
 }
 return true;
}
void RemoteBlinkClient::callback(const TeslaNimbleEvent &e){
 auto *s=owner.load(std::memory_order_acquire);if(!s)return;
 unsigned head=s->head.load(std::memory_order_relaxed),tail=s->tail.load(std::memory_order_acquire);
 if(head-tail>=8||e.size>20){s->overflow=true;return;}
 Message m{};m.kind=e.kind;m.epoch=e.epoch;m.status=e.status;m.size=static_cast<uint8_t>(e.size);
 if(e.size){if(!e.data){s->overflow=true;return;}memcpy(m.data,e.data,e.size);}
 s->messages[head%8]=m;s->head.store(head+1,std::memory_order_release);
}
void RemoteBlinkClient::retire(bool success){
 ok=success&&!cancelled;state=Draining;
 if(secure&&secure_started)secure.cancel(secure.context);
 (void)central->cancel_open(); // idempotent drain, never disconnect a foreign lease
}
void RemoteBlinkClient::poll(uint64_t now){
 if(!active()||!central)return;
 if((cancelled||overflow.load()||now>=until)&&state!=Draining)retire(false);
 unsigned tail_value=tail.load(std::memory_order_relaxed);
 while(tail_value!=head.load(std::memory_order_acquire)){
  Message m=messages[tail_value%8];tail.store(++tail_value,std::memory_order_release);
  if(m.epoch!=epoch)continue;
  if(m.kind==TeslaNimbleEvent::Disconnected){
   if(state!=Draining)retire(received);
   continue;
  }
  if(state==Draining)continue;
  if(m.status){retire(false);continue;}
  bool submitted=true;
  if(state==Opening&&m.kind==TeslaNimbleEvent::Open){state=Discovering;submitted=central->discover();}
  else if(state==Discovering&&m.kind==TeslaNimbleEvent::Discover){state=Subscribing;submitted=central->subscribe();}
  else if(state==Subscribing&&m.kind==TeslaNimbleEvent::Descriptor){
   if(secure){secure_started=secure.begin(secure.context,now,token[0]?token[0]:1);if(!secure_started){retire(false);continue;}}
   uint8_t out[17]={secure?secure.selection:(uint8_t)1};memcpy(out+1,token,16);state=Selecting;submitted=central->write(out,sizeof(out));
  }else if(state==Selecting&&m.kind==TeslaNimbleEvent::Write){
   state=WaitingClick;
  }else if(secure&&secure_started&&(state==WaitingClick||state==Selecting)){
   if(m.kind==TeslaNimbleEvent::Rx)submitted=secure.receive(secure.context,now,m.data,m.size);
   else if(m.kind==TeslaNimbleEvent::Write&&secure_writing)secure_writing=false;
  }else if(!secure&&(state==WaitingClick||state==Selecting)&&m.kind==TeslaNimbleEvent::Rx&&m.size==17&&m.data[0]==2&&!memcmp(m.data+1,token,16)){
   uint8_t out[17]={3};memcpy(out+1,token,16);state=ReceiptWriting;submitted=central->write(out,sizeof(out));
  }else if(state==ReceiptWriting&&m.kind==TeslaNimbleEvent::Write){received=true;retire(true);}
  if(!submitted)retire(false);
 }
 if(secure&&secure_started&&state==WaitingClick){
  uint8_t frame[20];size_t n=secure.frame(secure.context,now,frame);
  if(secure.failed(secure.context))retire(false);
  else if(secure.complete(secure.context)){trusted=!secure.normal_session;received=true;retire(true);}
  else if(n&&!secure_writing){
   secure_writing=central->write(frame,n);
   // Advance the immutable SUBMITTED fragment now, never a newly queued reply
   // on a delayed Write callback. Crypto committed receipt, not ATT acceptance,
   // governs pairing. Immediate submit/async write failures still retire false.
   if(secure_writing)secure.sent(secure.context,now);else retire(false);
  }
 }
 if(state==Draining){
  (void)central->cancel_open();
  // SDK terminal dispatch AND copied main queue are drained before lease reuse.
  if(central->idle()&&tail.load()==head.load()&&central->release_remote()){
   memset(token,0,sizeof(token));central=nullptr;owner.store(nullptr,std::memory_order_release);
   state=ok?Complete:Failed;
  }
 }
}
#endif
