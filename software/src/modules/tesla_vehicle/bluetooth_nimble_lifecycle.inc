#include <new>
#ifndef TESLA_BLE_MODULE_HOST_TEST
#include <Arduino.h>
#include <esp_bt.h>
#endif
extern "C" bool bleInUse(){return true;}
struct BluetoothLifecycle::Context {
 enum Phase{Starting,Active,Stopping,Stopped,Failed};
 std::atomic<Phase> phase{Starting};std::atomic<bool> stop{false},error{false},done{false},host_done{false};
 std::atomic<uint32_t> since{0};TaskHandle_t task=nullptr,host_task=nullptr;Handler handler=nullptr;
 TeslaNimbleCentral central;bool acquired=false;
};
std::atomic<BluetoothLifecycle::Context *> BluetoothLifecycle::owner{nullptr};
BluetoothLifecycle::BluetoothLifecycle()=default;
BluetoothLifecycle::~BluetoothLifecycle(){
 if(!context) { return; }
 context->stop=true;
 if((context->task&&!context->done.load(std::memory_order_acquire))||owner.load()==context.get()){context.release();return;}
 if(context->task)vTaskDelete(context->task);
}
bool BluetoothLifecycle::start(Handler handler){
 if(!bluetooth_supported()||context||latched)return false;
 bluetooth_memory::sample(bluetooth_memory::Stage::ContextBefore);context.reset(new(std::nothrow)Context);bluetooth_memory::sample(bluetooth_memory::Stage::ContextAfter);
 if(!context){latched=true;return false;}Context *expected=nullptr;
 if(!owner.compare_exchange_strong(expected,context.get())){latched=true;return false;}
 context->handler=handler;context->since=millis();
 if(xTaskCreatePinnedToCore(entry,"bluetooth",4096,context.get(),1,&context->task,0)!=pdPASS){latched=true;context->task=nullptr;owner=nullptr;return false;}return true;
}
void BluetoothLifecycle::request_stop(){if(context&&!context->stop.exchange(true))context->since=millis();}
bool BluetoothLifecycle::failed()const{return latched||(context&&context->error);}
bool BluetoothLifecycle::active()const{return context&&!failed()&&!context->stop&&context->phase==Context::Active;}
bool BluetoothLifecycle::stopped()const{return context&&!failed()&&context->done.load(std::memory_order_acquire)&&context->phase==Context::Stopped;}
bool BluetoothLifecycle::gatt_ready()const{return active()&&context->central.ready();}
TeslaNimbleCentral *BluetoothLifecycle::central(){return active()?&context->central:nullptr;}
const char *BluetoothLifecycle::phase()const{if(failed())return "error";if(!context||stopped())return "disabled";return active()?"active":context->stop?"stopping":"starting";}
void BluetoothLifecycle::supervise(){
 if(!context)return;
 if(context->central.discovery.overdue(millis())){latched=true;context->central.discovery.cancel("timeout");request_stop();}
 if(!context->done.load(std::memory_order_acquire)&&(context->phase!=Context::Active||context->stop)&&millis()-context->since.load()>15000){latched=true;context->stop=true;}
 if(context->done.load(std::memory_order_acquire)&&context->task){vTaskDelete(context->task);context->task=nullptr;}
}
void BluetoothLifecycle::gatt(const TeslaNimbleEvent &e){auto *c=owner.load();if(c&&!c->stop&&c->handler)c->handler(e);}
void BluetoothLifecycle::entry(void *p){run(*static_cast<Context *>(p));}
void BluetoothLifecycle::host_entry(void *p){auto &c=*static_cast<Context *>(p);c.central.run();c.host_done.store(true,std::memory_order_release);for(;;)vTaskSuspend(nullptr);}
bool BluetoothLifecycle::teardown(Context &c){
 if(!c.acquired)return false; // failed initialization is quarantined, never steal foreign stack
 if(!c.host_task)return c.central.deinitialize();
 // Cancel/terminate may return an error while its terminal event is already
 // queued. Keep the host alive for the SAME bounded drain in either case.
 // An error never establishes idle; missing terminal still retains ownership.
 c.central.quiesce();
 // Repeat the idempotent retirement step: a raced/unannounced link may only
 // become terminable after the first cancel returned.
 const auto drain=millis();for(unsigned step=1;!c.central.idle()&&millis()-drain<5000;++step){vTaskDelay(pdMS_TO_TICKS(5));if(step%20==0&&!c.central.idle())c.central.quiesce();}
 if(!c.central.idle()||!c.central.shutdown())return false;
 const auto start=millis();while(!c.host_done.load(std::memory_order_acquire)&&millis()-start<5000)vTaskDelay(pdMS_TO_TICKS(5));
 if(!c.host_done.load(std::memory_order_acquire)||!c.central.deinitialize())return false;
 vTaskDelete(c.host_task);c.host_task=nullptr;c.acquired=false;return true;
}
void BluetoothLifecycle::run(Context &c){
 bool ok=false;
 if(!c.stop&&r13_psram_ok()){
  c.acquired=c.central.initialize(gatt);bluetooth_memory::sample(bluetooth_memory::Stage::HostInit,true);
  if(c.acquired&&xTaskCreatePinnedToCore(host_entry,"nimble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,&c,configMAX_PRIORITIES-4,&c.host_task,CONFIG_BT_NIMBLE_PINNED_TO_CORE)==pdPASS){
   const auto start=millis();while(!c.stop&&!c.central.ready()&&!c.central.faulted()&&millis()-start<5000)vTaskDelay(pdMS_TO_TICKS(5));
   ok=c.central.ready();
   if(ok&&!c.stop){c.phase=Context::Active;bluetooth_memory::sample(bluetooth_memory::Stage::GattReady,true);
    while(!c.stop){bluetooth_memory::sample(bluetooth_memory::Stage::BtActive,true,1000);if(!r13_psram_ok()||c.central.faulted()||!c.central.scan_tick(millis())){ok=false;break;}vTaskDelay(pdMS_TO_TICKS(20));}}
  }else c.host_task=nullptr;
 }else if(c.stop){owner=nullptr;c.phase=Context::Stopped;c.done.store(true,std::memory_order_release);for(;;)vTaskSuspend(nullptr);}
 c.stop=true;c.since=millis();c.phase=Context::Stopping;bluetooth_memory::sample(bluetooth_memory::Stage::BtStopBefore,true);
 bool clean=teardown(c);bluetooth_memory::sample(bluetooth_memory::Stage::BtStopAfter,true);
 if(clean)owner=nullptr;
 c.error=!ok||!clean;c.phase=c.error?Context::Failed:Context::Stopped;c.done.store(true,std::memory_order_release);for(;;)vTaskSuspend(nullptr);
}
#if TESLA_BLE_MANUAL_VEHICLE_UI
bool BluetoothLifecycle::request_discovery(){return active()&&context->central.discovery.request(millis());}
bool BluetoothLifecycle::request_discovery(const TeslaBleDiscovery::Scope &s){return active()&&context->central.discovery.request(millis(),s);}
bool BluetoothLifecycle::discovery_matches(const TeslaBleDiscovery::Scope &s,uint32_t rev,const uint8_t *a,uint8_t t){return active()&&context->central.discovery.matches(millis(),s,rev,a,t);}
bool BluetoothLifecycle::discovery_authority_current(uint32_t rev){return active()&&context->central.discovery.authority_current(rev);}
bool BluetoothLifecycle::discovery_busy()const{return context&&context->central.discovery.busy();}
bool BluetoothLifecycle::discovery_snapshot(TeslaBleDiscovery::Snapshot &s){if(!context){s={};return true;}return context->central.discovery.snapshot(s);}
#endif
