#pragma once
#include <cstddef>
#include <cstdint>
#include <string.h>
// ATT-minimum-MTU framing, not authentication. Full nonce/transcript crypto and
// connection-generation admission are mandatory above this small wire cookie.
class RemoteFragments {
public:
 enum Result {Invalid,Partial,Complete,Duplicate};
 static size_t encode(uint8_t attempt,uint8_t type,const uint8_t *data,size_t size,size_t offset,uint8_t out[20]){
  if(!attempt||!data||!out||!valid_size(type,size)||offset>=size)return 0;
  size_t n=size-offset;if(n>14)n=14;
  out[0]=0xa6;out[1]=attempt;out[2]=type;out[3]=static_cast<uint8_t>(offset);out[4]=static_cast<uint8_t>(size);out[5]=1;
  memcpy(out+6,data+offset,n);return n+6;
 }
 Result feed(const uint8_t *p,size_t n,uint8_t attempt){
  if(bad||!p||n<7||n>20||p[0]!=0xa6||p[1]!=attempt||!attempt||p[5]!=1||!valid_size(p[2],p[4])||p[3]+n-6>p[4])return invalid();
  size_t offset=p[3],bytes=n-6;
  if(used&&(kind!=p[2]||length!=p[4]))return invalid();
  if(offset<used){
   if(offset+bytes>used||memcmp(buffer+offset,p+6,bytes))return invalid();
   return Duplicate;
  }
  if(offset!=used||done)return invalid();
  kind=p[2];length=p[4];memcpy(buffer+used,p+6,bytes);used+=bytes;
  done=used==length;return done?Complete:Partial;
 }
 void reset(){volatile uint8_t *p=buffer;for(unsigned i=0;i<96;i++)p[i]=0;used=length=kind=0;bad=done=false;}
 bool complete()const{return done&&!bad;}
 const uint8_t *data()const{return buffer;}
 size_t size()const{return length;}
 uint8_t type()const{return kind;}
private:
 uint8_t buffer[96]{},kind=0,length=0;size_t used=0;bool done=false,bad=false;
 static bool valid_size(uint8_t type,size_t n){return ((type==1||type==10||type==17)&&n==96)||(((type>=2&&type<=9)||(type>=11&&type<=16)||type==18||type==19||type==22)&&n==17)||(type==20&&n==32)||(type==21&&n==80);}
 Result invalid(){reset();bad=true;return Invalid;}
};
