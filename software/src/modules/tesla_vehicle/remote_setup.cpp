#if defined(TESLA_BLE_USE_NIMBLE) && TESLA_BLE_USE_NIMBLE
#include "remote_setup.h"
#include <cstring>
#include <limits>
std::atomic<RemoteSetup *> RemoteSetup::owner{nullptr};
bool RemoteSetup::start(TeslaNimbleCentral &c,uint64_t now,bool normal){
 if(busy()||epoch==UINT32_MAX||now>UINT64_MAX-30000)return false;
 RemoteSetup *expected=nullptr;if(!owner.compare_exchange_strong(expected,this))return false;
 central=&c;++epoch;head=tail=0;overflow=false;candidate_count=0;selected=false;chosen=0;
 until=now+30000;state=Scanning;scanning=true;normal_scan=normal;
 if(!c.scan_remote(epoch,callback,normal_scan)){state=Failed;central=nullptr;owner=nullptr;scanning=false;return false;}
 return true;
}
void RemoteSetup::callback(const TeslaNimbleEvent &event){
 auto *s=owner.load(std::memory_order_acquire);if(!s)return;
 if(event.kind!=TeslaNimbleEvent::RemoteCandidate&&event.kind!=TeslaNimbleEvent::RemoteScanDone)return;
 unsigned head=s->head.load(std::memory_order_relaxed),tail=s->tail.load(std::memory_order_acquire);
 if(head-tail>=8){s->overflow=true;return;}
 Message m{};m.epoch=event.epoch;m.status=event.status;m.done=event.kind==TeslaNimbleEvent::RemoteScanDone;
 if(!m.done){if(!event.data||event.size!=7)return;memcpy(m.data,event.data,7);}
 s->queue[head%8]=m;s->head.store(head+1,std::memory_order_release);
}
bool RemoteSetup::select(uint64_t now,uint32_t generation,unsigned index,const uint8_t nonce[16]){
 if(!busy()||state==Connecting||state==Draining||selected||generation!=epoch||index>=candidate_count||!nonce||now>=until)return false;
 chosen=index;memcpy(token,nonce,16);selected=true;state=Choosing;
 if(scanning&&!central->cancel_remote_scan()){selected=false;memset(token,0,16);cancel();terminal=Failed;return false;}
 return true;
}
void RemoteSetup::cancel(){
 if(!busy())return;
 terminal=Cancelled;state=Draining;
 if(scanning)(void)central->cancel_remote_scan();
 if(blink.active())blink.cancel();
}
void RemoteSetup::clear(){
 memset(token,0,16);for(auto &c:candidates)c={};candidate_count=0;
 central=nullptr;owner.store(nullptr,std::memory_order_release);
}
void RemoteSetup::poll(uint64_t now){
 if(!busy()||!central)return;
 if((now>=until||overflow.load())&&state!=Draining){cancel();terminal=overflow.load()?Failed:Expired;}
 unsigned tail_value=tail.load(std::memory_order_relaxed);
 while(tail_value!=head.load(std::memory_order_acquire)){
  Message m=queue[tail_value%8];tail.store(++tail_value,std::memory_order_release);
  if(m.epoch!=epoch||m.done||state==Draining||selected)continue;
  bool duplicate=false;
  for(unsigned i=0;i<candidate_count;i++)if(candidates[i].type==m.data[6]&&!memcmp(candidates[i].address,m.data,6))duplicate=true;
  if(!duplicate&&candidate_count<4){auto &c=candidates[candidate_count++];memcpy(c.address,m.data,6);c.type=m.data[6];}
 }
 if(scanning&&central->idle()&&tail.load()==head.load()){
  if(!central->release_remote())return;
  scanning=false;
 }
 if(state==Draining){
  if(scanning){(void)central->cancel_remote_scan();return;}
  blink.poll(now);
  if(!blink.active()){state=terminal;clear();}
  return;
 }
 if(selected&&!scanning&&state!=Connecting){
  auto peer=candidates[chosen];
  state=Connecting;
  if(!blink.begin(*central,now,peer.address,peer.type,token)){state=Failed;clear();return;}
  memset(token,0,16);
 }
 if(state==Connecting){
  blink.poll(now);
  if(!blink.active()){
   state=blink.normal_complete()?SessionComplete:blink.paired()?Paired:blink.phase()==RemoteBlinkClient::Complete&&blink.consent_received()?ConsentReceived:Failed;
   clear();
  }
 }else if(!scanning){
  // Repeat only while the explicitly opened window remains. Busy Tesla work
  // wins admission; do not cancel or disconnect it to make room.
  if(central->scan_remote(epoch,callback,normal_scan)){scanning=true;state=Scanning;}
 }
}
#endif
