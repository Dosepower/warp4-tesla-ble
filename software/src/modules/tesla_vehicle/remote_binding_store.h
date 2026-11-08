#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
// Dedicated remote namespace. Never import BLE addresses/RSSI as identity.
// Main task only. Backend must commit atomically; verified readback precedes ACK.
class RemoteBindingStore {
public:
 struct Backend {void *context;int(*read)(void *,uint8_t *,size_t);bool(*write)(void *,const uint8_t *,size_t);};
 struct Binding {uint8_t state=0,peer[32]{},key[32]{};uint64_t sequence=0;char name[33]{};};
 explicit RemoteBindingStore(Backend b):backend(b){}
 ~RemoteBindingStore(){for(auto &b:bindings)wipe(&b,sizeof(b));}
 bool load(){
  if(fault)return false;
  for(auto &b:bindings)wipe(&b,sizeof(b));
  uint8_t wire[WIRE_SIZE]{};int rc=backend.read(backend.context,wire,sizeof(wire));
  if(rc==0){loaded=true;return true;}
  if(rc!=1||!decode(wire)){fault=true;for(auto &b:bindings)wipe(&b,sizeof(b));wipe(wire,sizeof(wire));return false;}
  loaded=true;wipe(wire,sizeof(wire));return true;
 }
 bool quarantined()const{return fault;}
 unsigned count()const{unsigned n=0;for(auto &b:bindings)if(b.state==2)n++;return n;}
 unsigned prepared_count()const{unsigned n=0;for(auto &b:bindings)if(b.state==1||b.state==3)n++;return n;}
 const Binding *find(const uint8_t peer[32])const{
  if(!ready())return nullptr;
  for(auto &b:bindings){if(b.state&&equal(b.peer,peer,32))return &b;}
  return nullptr;
 }
 const Binding &slot(unsigned i)const{return bindings[i<4?i:0];}
 bool prepare(const uint8_t peer[32],const uint8_t key[32],const char *name){
  if(!ready()||!peer||!key||!name||!nonzero(peer,32)||!nonzero(key,32)||strlen(name)>32||find(peer))return false;
  for(auto &b:bindings)if(!b.state){b.state=1;memcpy(b.peer,peer,32);memcpy(b.key,key,32);memcpy(b.name,name,strlen(name)+1);return persist();}
  return false;
 }
 // Enrollment controller alone calls after authenticated commit proof.
 // State1=prepared,3=commit-pending (untrusted after boot),2=active.
 bool verify_prepared(const uint8_t peer[32],const uint8_t key[32]){
  const auto *binding=find(peer);if(!binding||(binding->state!=1&&binding->state!=3)||!equal(binding->key,key,32))return false;
  uint8_t expected[WIRE_SIZE]{},actual[WIRE_SIZE]{};encode(expected);
  bool ok=backend.read(backend.context,actual,sizeof(actual))==1&&equal(expected,actual,sizeof(actual));
  wipe(expected,sizeof(expected));wipe(actual,sizeof(actual));
  if(!ok){fault=true;for(auto &b:bindings)wipe(&b,sizeof(b));}return ok;
 }
 bool commit_prepared(const uint8_t peer[32],const uint8_t key[32]){
  if(!verify_prepared(peer,key))return false;
  for(auto &b:bindings)if(b.state==1&&equal(b.peer,peer,32)){b.state=3;return persist();}
  return false;
 }
 bool recover_stage(const uint8_t peer[32],unsigned phase){
  const auto *b=find(peer);if(!b||phase<2||phase>3)return false;
  uint8_t expected[WIRE_SIZE]{},actual[WIRE_SIZE]{};encode(expected);
  bool ok=backend.read(backend.context,actual,sizeof(actual))==1&&equal(expected,actual,sizeof(actual));wipe(expected,sizeof(expected));wipe(actual,sizeof(actual));
  if(!ok){fault=true;for(auto &v:bindings)wipe(&v,sizeof(v));return false;}
  if(b->state==2)return true;
  if(phase==2)return b->state==3||commit_prepared(peer,b->key);
  return b->state==3&&activate(peer);
 }
 bool activate(const uint8_t peer[32]){
  if(!ready())return false;
  for(auto &b:bindings)if(b.state==3&&equal(b.peer,peer,32)){b.state=2;return persist();}
  return false;
 }
 bool forget(const uint8_t peer[32]){
  if(!ready())return false;
  for(auto &b:bindings)if(b.state&&equal(b.peer,peer,32)){wipe(&b,sizeof(b));return persist();}
  return false;
 }
 bool reserve(const uint8_t peer[32],uint64_t &sequence){
  sequence=0;if(!ready())return false;
  for(auto &b:bindings)if(b.state==2&&equal(b.peer,peer,32)){
   if(b.sequence==UINT64_MAX)return false;
   ++b.sequence;if(!persist())return false;sequence=b.sequence;return true;
  }
  return false;
 }
 static bool equal(const uint8_t *a,const uint8_t *b,size_t n){uint8_t diff=0;for(size_t i=0;i<n;i++)diff|=a[i]^b[i];return diff==0;}
 static void wipe(void *ptr,size_t n){volatile uint8_t *p=static_cast<volatile uint8_t *>(ptr);while(n--)*p++=0;}
private:
 static constexpr size_t ENTRY=106,WIRE_SIZE=4+4*ENTRY;
 Backend backend;Binding bindings[4]{};bool loaded=false,fault=false;
 bool ready()const{return loaded&&!fault;}
 static bool nonzero(const uint8_t *p,size_t n){uint8_t any=0;while(n--)any|=*p++;return any!=0;}
 void encode(uint8_t wire[WIRE_SIZE])const{
  memcpy(wire,"WRB1",4);
  for(unsigned i=0;i<4;i++){
   auto &b=bindings[i];auto *p=wire+4+i*ENTRY;p[0]=b.state;memcpy(p+1,b.peer,32);memcpy(p+33,b.key,32);
   for(unsigned j=0;j<8;j++)p[65+j]=static_cast<uint8_t>(b.sequence>>(56-8*j));
   memcpy(p+73,b.name,33);
  }
 }
 bool decode(const uint8_t wire[WIRE_SIZE]){
  if(memcmp(wire,"WRB1",4))return false;
  for(unsigned i=0;i<4;i++){
   auto &b=bindings[i];const auto *p=wire+4+i*ENTRY;
   if(p[0]>3||p[105]!=0)return false;
   b.state=p[0];memcpy(b.peer,p+1,32);memcpy(b.key,p+33,32);memcpy(b.name,p+73,33);b.sequence=0;
   for(unsigned j=0;j<8;j++)b.sequence=(b.sequence<<8)|p[65+j];
   if(b.state&&(!nonzero(b.peer,32)||!nonzero(b.key,32)))return false;
   if(!b.state&&nonzero(p+1,ENTRY-1))return false;
   for(unsigned j=0;j<i;j++)if(b.state&&bindings[j].state&&equal(b.peer,bindings[j].peer,32))return false;
  }
  return true;
 }
 bool persist(){
  uint8_t wire[WIRE_SIZE]{},readback[WIRE_SIZE]{};encode(wire);
  bool ok=backend.write(backend.context,wire,sizeof(wire))&&backend.read(backend.context,readback,sizeof(readback))==1&&equal(wire,readback,sizeof(wire));
  wipe(wire,sizeof(wire));wipe(readback,sizeof(readback));
  if(!ok){fault=true;for(auto &b:bindings)wipe(&b,sizeof(b));}
  return ok;
 }
};
