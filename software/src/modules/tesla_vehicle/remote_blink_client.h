#pragma once
#include "tesla_nimble_central.h"
#include "remote_enrollment_port.h"
// Single shared-host ATT transport. Empty enrollment port is untrusted preview;
// production installs crypto/durable enrollment. Neither path writes charging.
class RemoteBlinkClient {
public:
 enum Phase:uint8_t {Idle,Opening,Discovering,Subscribing,Selecting,WaitingClick,
                     ReceiptWriting,Draining,Complete,Failed};
 bool begin(TeslaNimbleCentral &,uint64_t now,const uint8_t address[6],uint8_t type,const uint8_t token[16]);
 void poll(uint64_t now);
 bool set_enrollment(RemoteEnrollmentPort p){if(active())return false;secure=p;return true;}
 bool normal_complete()const{return secure.normal_session&&received&&state==Complete;}
 bool paired()const{return trusted&&state==Complete;}
 void cancel(){cancelled=true;}
 Phase phase()const{return state;}
 uint64_t generation()const{return epoch;}
 bool consent_received()const{return received;}
 bool active()const{return state!=Idle&&state!=Complete&&state!=Failed;}
private:
 struct Message {TeslaNimbleEvent::Kind kind;uint64_t epoch;int status;uint8_t size,data[20];};
 static std::atomic<RemoteBlinkClient *> owner;
 static void callback(const TeslaNimbleEvent &);
 void retire(bool success);
 TeslaNimbleCentral *central=nullptr;
 Message messages[8]{};
 std::atomic<unsigned> head{0},tail{0};
 std::atomic<bool> overflow{false};
 Phase state=Idle;
 uint64_t epoch=0,until=0;
 uint8_t token[16]{};
 bool received=false,cancelled=false,ok=false,trusted=false,secure_started=false,secure_writing=false;
 RemoteEnrollmentPort secure{};
};
