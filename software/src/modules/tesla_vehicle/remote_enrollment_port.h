#pragma once
#include <cstdint>
#include <cstddef>
// Transport dependency inversion. Empty port is preview-only, never trusted.
struct RemoteEnrollmentPort {
 void *context=nullptr;
 bool(*begin)(void *,uint64_t,uint8_t)=nullptr;
 bool(*receive)(void *,uint64_t,const uint8_t *,size_t)=nullptr;
 size_t(*frame)(void *,uint64_t,uint8_t *)=nullptr;
 void(*sent)(void *,uint64_t)=nullptr;
 bool(*complete)(void *)=nullptr;
 bool(*failed)(void *)=nullptr;
 void(*cancel)(void *)=nullptr;
 uint8_t selection=1;
 bool normal_session=false;
 explicit operator bool()const{return context&&begin&&receive&&frame&&sent&&complete&&failed&&cancel;}
};
