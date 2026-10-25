#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include "tesla_ble_discovery.h"
#ifdef TESLA_NIMBLE_HOST_TEST
#include "nimble_platform.h"
#else
extern "C" {
#include <nimble/nimble_port.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <os/os_mbuf.h>
}
#endif
// Value-only callback boundary. No Bluedroid types or runtime-owned cookies.
struct TeslaNimbleEvent {
 enum Kind : uint8_t { Open=2,Discover=3,Descriptor=6,Write=7,Close=8,Disconnected,Rx,Security };
 Kind kind;uint64_t epoch;uint16_t connection,handle;int status;
 const uint8_t *data=nullptr;size_t size=0; // borrowed only for this synchronous dispatch
};
class TeslaNimbleCentral {
public:
 using Handler=void(*)(const TeslaNimbleEvent &);
 bool initialize(Handler);
 void run(); // caller owns host task and final ACK
 bool quiesce(); // close admission and cancel/terminate, host must keep running
 bool shutdown(); // only after quiesce/idle; caller MUST join run before deinit
 bool deinitialize();
 bool ready()const{return synchronized.load()&&!failed.load()&&!retiring.load();}
 bool faulted()const{return failed.load();}
 bool idle()const{return !busy.load();}
 bool open(const uint8_t *address,uint8_t type,uint64_t epoch);
 bool discover();
 bool subscribe();
 bool write(const uint8_t *,size_t);
 bool close();
 // Retire this connection attempt, not the host. Only idle() proves drainage.
 int cancel_open();
 bool scan_tick(uint32_t now);
 TeslaBleDiscovery discovery;
 uint16_t tx()const{return write_handle;}uint16_t rx()const{return notify_handle;}
private:
 static std::atomic<TeslaNimbleCentral *> owner;
 static void sync();static void reset(int);
 static int gap(ble_gap_event *,void *);static int scan(ble_gap_event *,void *);
 static int service(uint16_t,const ble_gatt_error *,const ble_gatt_svc *,void *);
 static int characteristic(uint16_t,const ble_gatt_error *,const ble_gatt_chr *,void *);
 static int descriptor(uint16_t,const ble_gatt_error *,uint16_t,const ble_gatt_dsc *,void *);
 static int written(uint16_t,const ble_gatt_error *,ble_gatt_attr *,void *);
 void emit(TeslaNimbleEvent::Kind,int,uint16_t=0,const uint8_t * =nullptr,size_t=0);
 void finish_discovery(int);
 struct Cookie {TeslaNimbleCentral *owner=nullptr;uint64_t epoch=0;};
 Cookie cookie; // immutable from successful open submission through disconnect barrier
 std::atomic<bool> synchronized{false},failed{false},busy{false},running{false},retiring{false},cancelling_open{false};
 std::atomic<uint16_t> connection{0xffff};
 Handler handler=nullptr;bool initialized=false;uint8_t own_type=0;
 uint16_t service_start=0,service_end=0,write_handle=0,notify_handle=0,descriptor_end=0,cccd=0;
 unsigned services=0,writes=0,notifies=0,descriptors=0;uint8_t properties=0;
 std::atomic<int> pending{0}; // one request, callbacks terminal before disconnect event
 struct Advertisement{uint8_t address[6]{},type=255,data[31]{};uint8_t size=0;};
 Advertisement advertisements[8]{};unsigned next_advertisement=0;
};
