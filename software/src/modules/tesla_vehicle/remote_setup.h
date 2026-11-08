#pragma once
#include "remote_blink_client.h"
// Bounded shared-host setup; only a committed enrollment port can yield Paired.
class RemoteSetup {
public:
 enum Phase:uint8_t {Idle,Scanning,Choosing,Connecting,ConsentReceived,Failed,Cancelled,Draining,Expired,Paired,SessionComplete};
 bool set_enrollment(RemoteEnrollmentPort p){return (!busy()||(state==Scanning&&!selected))&&blink.set_enrollment(p);}
 struct Candidate {uint8_t address[6]{},type=0;};
 bool start(TeslaNimbleCentral &,uint64_t now,bool normal=false);
 bool select(uint64_t now,uint32_t generation,unsigned index,const uint8_t nonce[16]);
 void cancel();
 void poll(uint64_t now);
 bool busy()const{return state==Scanning||state==Choosing||state==Connecting||state==Draining;}
 Phase phase()const{return state;}
 uint32_t generation()const{return epoch;}
 unsigned count()const{return candidate_count;}
 const Candidate &candidate(unsigned index)const{return candidates[index<4?index:0];}
 uint64_t remaining(uint64_t now)const{return busy()&&until>now?until-now:0;}
private:
 struct Message {uint64_t epoch;int status;bool done;uint8_t data[7];};
 static std::atomic<RemoteSetup *> owner;
 static void callback(const TeslaNimbleEvent &);
 TeslaNimbleCentral *central=nullptr;
 RemoteBlinkClient blink;
 Candidate candidates[4]{};
 unsigned candidate_count=0;
 Message queue[8]{};
 std::atomic<unsigned> head{0},tail{0};
 std::atomic<bool> overflow{false};
 Phase state=Idle,terminal=Cancelled;
 uint32_t epoch=0;
 uint64_t until=0;
 bool scanning=false,selected=false,normal_scan=false;
 unsigned chosen=0;
 uint8_t token[16]{};
 void clear();
};
