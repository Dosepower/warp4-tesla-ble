#pragma once
#include <cstdint>
#include <limits>
// Main-task only. Read-only evidence of native CM echo; never a current writer.
// CM transport is the existing LAN trust boundary, not cryptographic evcc proof.
// BLE layer MUST authenticate/correlate command ACKs separately.
class ManagerModeObservation {
public:
 void observe(uint64_t now,uint8_t mode,bool management_enabled){
  valid_=management_enabled&&mode<=9&&mode!=4&&revision_!=std::numeric_limits<uint64_t>::max();
  if(!valid_)return;
  ++revision_;received_=now;mode_=mode;
 }
 bool fresh(uint64_t now)const{return valid_&&now>=received_&&now-received_<=5000;}
 bool acknowledges(uint64_t after,uint64_t now,uint8_t wanted)const{
  return fresh(now)&&revision_>after&&mode_==wanted;
 }
 uint64_t revision()const{return revision_;}
 uint64_t received_at()const{return received_;}
 uint8_t mode()const{return mode_;}
private:
 bool valid_=false;
 uint64_t revision_=0,received_=0;
 uint8_t mode_=4;
};
