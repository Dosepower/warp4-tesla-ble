#pragma once
#include "../tesla_vehicle/bluetooth_support.h"
#include "../tesla_vehicle/bluetooth_features.h"
#include "../tesla_vehicle/bluetooth_config_store.h"
#include "../tesla_vehicle/bluetooth_lifecycle.h"
#include "../tesla_vehicle/bluetooth_master_bench.h"
#ifdef TESLA_BLE_MODULE_HOST_TEST
#include "module_platform.h"
#else
#include "config.h"
#include "module.h"
#endif
#if WARP_FEATURE_REMOTE
#include "../tesla_vehicle/remote_setup.h"
#include "../tesla_vehicle/remote_binding_backend.h"
#include "../tesla_vehicle/remote_operation_backend.h"
#include "../tesla_vehicle/remote_enrollment_link.h"
#include "../tesla_vehicle/remote_recovery_link.h"
#include "../tesla_vehicle/remote_session_link.h"
#include "../tesla_vehicle/remote_telemetry.h"
#endif

// Shared lifecycle owner. Exactly one selected module derives this class;
// remote-only never constructs the vehicle protocol, storage or worker.
class BluetoothCore : public IModule {
public:
    void pre_setup() override;
    void setup() override;
    void register_urls() override;
    void loop() override;
protected:
    friend struct TeslaBleModuleTest;
    virtual bool feature_busy() const { return false; }
    virtual void publish_bluetooth_state();
    ConfigRoot empty_command;
    ConfigRoot bluetooth_config, bluetooth_update, bluetooth_state;
#if WARP_FEATURE_REMOTE
    ConfigRoot remote_config,remote_update,remote_state,remote_candidates,remote_candidate,remote_select;
    ConfigRoot remote_bindings,remote_binding;
    RemoteSetup remote_setup;
    RemoteBindingStore remote_store{{nullptr,RemoteBindingBackend::read,RemoteBindingBackend::write}};
    RemoteOperationJournal remote_operations{{nullptr,RemoteOperationBackend::read,RemoteOperationBackend::write}};
    bool remote_operations_ok=false;
    static bool remote_entropy(void *,uint8_t *,size_t);
    static bool remote_enrollment_save(void *,const uint8_t *,const uint8_t *,unsigned);
    RemoteEnrollmentLink remote_enrollment{false,{this,remote_entropy,remote_enrollment_save}};
    static bool remote_recovery_load(void *,const uint8_t *,uint8_t *,uint8_t *,unsigned *);
    static bool remote_recovery_save(void *,unsigned);
    RemoteRecoveryLink remote_recovery{false,{this,remote_entropy,remote_recovery_load,remote_recovery_save}};
    uint8_t remote_recover_id[32]{};
    bool remote_recovering=false;
    static bool remote_session_load(void *,const uint8_t *,uint8_t *,uint8_t *);
    static bool remote_session_reserve(void *,const uint8_t *,uint64_t *);
    static bool remote_session_respond(void *,const uint8_t *,uint8_t *);
    static bool remote_session_snapshot(void *,const uint8_t *,uint8_t *);
    static bool remote_session_pending(void *);
    static int remote_session_finish(void *,const uint8_t *,uint8_t *);
    static void remote_session_abort(void *);
    static void remote_session_deliver(void *,const uint8_t *){}
    bool remote_response_waiting=false;
    uint64_t remote_response_until=0,remote_response_revision=0;
    RemoteSessionLink remote_session{false,{this,remote_entropy,remote_session_load,remote_session_reserve,remote_session_respond,remote_session_deliver,remote_session_pending,remote_session_finish,remote_session_abort}};
    RemoteSocObservation remote_soc;
    bool remote_normal=false;
    uint64_t remote_next_scan=0,remote_scan_until=0;
    ConfigRoot remote_recover,remote_forget;
    bool remote_bindings_ok=false;
    void remote_publish_bindings();
    bool remote_storage_quarantined=false,remote_commit_ok=true;
    void remote_pre_setup();
    void remote_load();
    void remote_register_urls();
    void remote_loop();
    bool remote_available() const;
#endif
    bool bluetooth_storage_quarantined=false;
    bool load_bluetooth(bool &desired);
    bool persist_bluetooth(bool desired);
    bool bluetooth_boot_enabled=false;
    bool bluetooth_commit_ok=true;
    void register_bluetooth_urls();
    bool save_bluetooth(bool desired,String &errmsg);
    bool restart_bluetooth(String &errmsg);
    BluetoothLifecycle bluetooth;
    bool bluetooth_restart_pending=false;
    bool service_start_requested=false;
#if TESLA_BLE_R13_MASTER_BENCH
    BluetoothMasterBench master_bench;
    ConfigRoot bench_state;
    bool bench_restart_pending=false;
    char bench_line[16]{};
    unsigned bench_line_size=0;
    bool bench_line_overflow=false;
#endif
};
