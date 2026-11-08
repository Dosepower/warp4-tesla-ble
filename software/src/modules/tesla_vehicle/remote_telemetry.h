#pragma once
#include <stdint.h>
#include <cmath>
// Main-task observation only after actual Tesla authenticated/correlated
// active-profile acceptance. An EV switch, reset, future/stale sample or invalid
// source is unavailable; never transfer a retained value to another identity.
class RemoteSocObservation {
public:
 void observe(uint64_t received,uint64_t generation,int index,uint64_t mac,bool authenticated,bool correlated,float value){
  valid=authenticated&&correlated&&generation!=UINT64_MAX&&index>=0&&mac&&std::isfinite(value)&&value>=0&&value<=100;
  at=received;gen=generation;profile=index;identity=mac;soc=value;
 }
 bool read(uint64_t now,uint64_t generation,int index,uint64_t mac,unsigned &value,uint32_t &age)const{
  value=0;age=0;if(!valid||generation!=gen||index!=profile||mac!=identity||now<at||now-at>5000)return false;
  value=(unsigned)std::lround(soc);age=(uint32_t)(now-at);return true;
 }
private:
 bool valid=false;uint64_t at=0,gen=0,identity=0;int profile=-1;float soc=0;
};
