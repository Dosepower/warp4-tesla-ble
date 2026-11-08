#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Public correlation metadata only. No root/private keys or telemetry values.
// Persist intent -> issued BEFORE invoking any writer -> final observed result.
// Existing prepared/issued records are UNKNOWN after restart, NEVER permission
// to reissue. A final record is historical, not fresh applied-mode evidence.
class RemoteOperationJournal {
public:
 static constexpr size_t WIRE_SIZE=180;
 struct Backend{void*context;int(*read)(void*,uint8_t*,size_t);bool(*write)(void*,const uint8_t*,size_t);};
 struct Entry{uint8_t peer[32]{},opcode=0,phase=0,outcome=0;uint64_t operation=0;};
 enum Admission{Fresh,Existing,Rejected,Fault};
 explicit RemoteOperationJournal(Backend io,unsigned capacity=4):io(io),capacity(capacity){}
 bool load(){
  if(fault||capacity<1||capacity>4)return false;for(auto&e:entries)e=Entry{};uint8_t wire[WIRE_SIZE]{};
  int r=io.read(io.context,wire,sizeof(wire));if(r==0){loaded=true;return true;}
  if(r!=1||!decode(wire)){fault=true;for(auto&e:entries)e=Entry{};return false;}loaded=true;return true;
 }
 bool quarantined()const{return fault;}
 const Entry*find(const uint8_t*peer)const{if(!loaded||fault||!peer)return nullptr;for(auto&e:entries)if(e.phase&&!memcmp(peer,e.peer,32))return &e;return nullptr;}
 Admission begin(const uint8_t*peer,uint64_t operation,uint8_t opcode){
  if(!loaded||fault)return Fault;if(!peer||!nonzero(peer,32)||!operation||opcode<3||opcode>6)return Rejected;
  Entry*slot=nullptr;
  for(unsigned i=0;i<capacity;i++)if(entries[i].phase&&!memcmp(entries[i].peer,peer,32)){slot=&entries[i];break;}
  if(slot){if(operation==slot->operation)return opcode==slot->opcode?Existing:Rejected;if(operation<slot->operation||slot->phase!=3)return Rejected;}
  else for(unsigned i=0;i<capacity;i++)if(!entries[i].phase){slot=&entries[i];break;}
  if(!slot)return Rejected;*slot=Entry{};memcpy(slot->peer,peer,32);slot->operation=operation;slot->opcode=opcode;slot->phase=1;return persist()?Fresh:Fault;
 }
 bool issue(const uint8_t*peer,uint64_t operation){return advance(peer,operation,1,2,0);}
 bool finish(const uint8_t*peer,uint64_t operation,uint8_t outcome){return outcome<=2&&advance(peer,operation,2,3,outcome);}
 // Only after confirmed credential removal AND verified root-store absence.
 // Never use this to retry an operation while its root remains authorized.
 bool retire(const uint8_t*peer,bool binding_removed){if(!binding_removed||!loaded||fault||!peer)return false;for(auto&e:entries)if(e.phase&&!memcmp(e.peer,peer,32)){e=Entry{};return persist();}return true;}
private:
 Backend io;unsigned capacity;Entry entries[4]{};bool loaded=false,fault=false;
 static bool nonzero(const uint8_t*p,size_t n){uint8_t v=0;while(n--)v|=*p++;return v!=0;}
 bool advance(const uint8_t*peer,uint64_t operation,uint8_t from,uint8_t to,uint8_t outcome){
  if(!loaded||fault||!peer)return false;for(auto&e:entries)if(e.phase==from&&e.operation==operation&&!memcmp(e.peer,peer,32)){e.phase=to;e.outcome=outcome;return persist();}return false;
 }
 void encode(uint8_t*wire)const{memcpy(wire,"WOJ1",4);for(unsigned i=0;i<4;i++){auto*p=wire+4+i*44;auto&e=entries[i];memcpy(p,e.peer,32);for(unsigned k=0;k<8;k++)p[32+k]=(uint8_t)(e.operation>>(56-8*k));p[40]=e.opcode;p[41]=e.phase;p[42]=e.outcome;p[43]=0;}}
 bool decode(const uint8_t*wire){
  if(memcmp(wire,"WOJ1",4))return false;for(unsigned i=0;i<4;i++){auto*p=wire+4+i*44;auto&e=entries[i];if(!p[41]){if(nonzero(p,44))return false;continue;}
   if(i>=capacity||p[41]>3||p[40]<3||p[40]>6||p[42]>2||(p[41]<3&&p[42])||p[43]||!nonzero(p,32))return false;
   memcpy(e.peer,p,32);for(unsigned k=0;k<8;k++)e.operation=(e.operation<<8)|p[32+k];if(!e.operation)return false;e.opcode=p[40];e.phase=p[41];e.outcome=p[42];
   for(unsigned k=0;k<i;k++)if(entries[k].phase&&!memcmp(entries[k].peer,e.peer,32))return false;
  }return true;
 }
 bool persist(){uint8_t expected[WIRE_SIZE]{},actual[WIRE_SIZE]{};encode(expected);bool ok=io.write(io.context,expected,sizeof(expected))&&io.read(io.context,actual,sizeof(actual))==1&&!memcmp(expected,actual,sizeof(actual));if(!ok){fault=true;for(auto&e:entries)e=Entry{};}return ok;}
};
