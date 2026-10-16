#!/usr/bin/env python3
"""No-RF-payload rehearsal: real bench/crypto, injected SDK only."""
from pathlib import Path
import re,subprocess,tempfile,unittest
import test_iram_bench_discovery as engine
import test_iram_bench_pairing as pair

class BenchRehearsalTests(unittest.TestCase):
    def test_resource_breach_after_host_init_is_sticky_and_cleanup_runs(self):
        harness=engine.HARNESS.replace('memcpy(p.scan_rst.ble_adv+5,"S0123456789abcdefC",18);','char vin[18]{},name[19];memset(vin,\'A\',17);assert(BenchPairing::vehicle_name(vin,name));memcpy(p.scan_rst.ble_adv+5,name,18);')
        harness=harness.replace('int esp_bluedroid_init() { return call("host.init"); }','int esp_bluedroid_init() { int rc=call("host.init");if(armed){resource_bad=true;}return rc; }').replace('int esp_bluedroid_deinit() { return call("host.deinit"); }','int esp_bluedroid_deinit() { resource_bad=false;return call("host.deinit"); }')
        harness=harness.replace('static std::vector<std::string> calls;','static bool armed=false;\nstatic std::vector<std::string> calls;')
        main=r'''
int main(int argc,char **argv){assert(argc==2||argc==3);resource_metric=argv[1];auto &d=tesla_ble_bench_discovery;char vin[18]{};memset(vin,'A',17);
assert(d.request_scan()==202);worker(context);assert(d.request_pair_prepare(0,vin)==202);worker(context);
armed=true;calls.clear();assert(d.request_pair_rehearsal()==202);worker(context);auto s=d.snapshot();
if(resource_metric=="boundary"){assert(!s.failure_code&&!s.resource_gate_failed&&s.teardown_complete&&s.stack_min_bytes==2048);return 0;}
assert(s.failure_code&&s.restart_required&&s.teardown_complete&&!s.worker_active);
assert(!strcmp(s.failure_stage,"host.init"));
assert(s.resource_gate_failed&&s.resource_gate_mask);assert(!strcmp(s.resource_failure.phase,"after"));
assert(s.resource_gate_mask==(resource_metric=="free"?1u:(resource_metric=="largest"?2u:(resource_metric=="minimum"?4u:8u))));
assert(std::find(calls.begin(),calls.end(),"host.enable")==calls.end());
for(const char *name:{"host.deinit","controller.disable","controller.deinit"})assert(std::find(calls.begin(),calls.end(),name)!=calls.end());
TeslaBleBenchDiscovery::ResourceSample row;for(unsigned i=0;d.resource_sample(i,row);++i)assert(strcmp(row.stage,"rehearsal.local"));
assert(!resource_bad);assert(s.internal8_free>=8192&&s.internal8_largest>=4096&&s.internal8_min>=8192);
assert(s.resource_failure.internal8_free<8192||s.resource_failure.internal8_largest<4096||s.resource_failure.internal8_min<8192||s.resource_failure.stack_min_bytes<2048);
const int failure=s.failure_code;assert(d.request_pair_start()==409&&d.request_pair_verify()==409&&d.request_pair_rehearsal()==409);
if(argc==3){mode="task_fail";assert(d.request_pair_delete()==503);s=d.snapshot();assert(s.resource_gate_failed&&s.failure_code==failure&&!strcmp(s.failure_stage,"host.init"));return 0;}
// Explicit manual key cleanup is permitted, but must never clear the latch.
assert(d.request_pair_delete()==202);worker(context);s=d.snapshot();
assert(s.resource_gate_failed&&s.failure_code==failure&&s.restart_required&&s.teardown_complete);
assert(!strcmp(s.resource_failure.stage,"host.init")&&!strcmp(s.failure_stage,"host.init"));
d.supervise();s=d.snapshot();assert(s.failure_code==failure&&!strcmp(s.failure_stage,"host.init"));assert(sent_wire.empty());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            t=Path(directory);engine.BenchDiscoveryTests().build(t,main=main,harness=harness)
            for metric in ('free','largest','minimum','stack','boundary'):
                for extra in ([],['cleanup_oom']) if metric!='boundary' else ([],):
                    for p in (t/'store').rglob('*'):
                        if p.is_file():p.unlink()
                    result=subprocess.run([str(t/'test'),metric,*extra],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stderr)
    def test_mesh_removal_without_stack_or_security_shrink(self):
        config=(engine.MODULE.parents[2]/'lib-builder/defconfig.warp4-ble-bench').read_text()
        self.assertIn('# CONFIG_BLE_MESH is not set',config)
        self.assertNotIn('\nCONFIG_BLE_MESH=y',config)
        self.assertIn('CONFIG_BT_BTC_TASK_STACK_SIZE=8192',config)
        self.assertIn('CONFIG_BT_BTU_TASK_STACK_SIZE=8192',config)
    def test_resource_checks_are_nonallocating_and_thresholds_unchanged(self):
        source=(engine.MODULE/'tesla_ble_bench_discovery.cpp').read_text()
        method=source.split('bool TeslaBleBenchDiscovery::measure(',1)[1].split('bool TeslaBleBenchDiscovery::stage(',1)[0]
        method='\n'.join(line.split('//',1)[0] for line in method.splitlines())
        for forbidden in ('new ', 'malloc(', 'printf(', 'String ', 'snapshot('):self.assertNotIn(forbidden,method)
        header=(engine.MODULE/'tesla_ble_bench_discovery.h').read_text()
        for expected in ('MIN_INTERNAL8_FREE=8192','MIN_INTERNAL8_LARGEST=4096','MIN_INTERNAL8_MIN=8192','MIN_WORKER_WATERMARK=2048','REHEARSAL_WORKER_BYTES=8192','PAIR_WORKER_BYTES=8192'):self.assertIn(expected,header)
    def test_strict_api_exposes_explicit_rehearsal_and_stage_samples(self):
        source=(engine.MODULE/'tesla_ble_bench_discovery_api.cpp').read_text()
        self.assertIn('server.on("/tesla_vehicle/bench_pair_rehearsal"',source)
        self.assertIn('request_pair_rehearsal()',source)
        self.assertIn('resource_sample(',source)
    def test_transport_free_local_crypto(self):
        source=(engine.MODULE/'tesla_ble_bench_pairing.cpp').read_text()
        self.assertIn('bool BenchPairing::rehearse_local()',source)
        method=source.split('bool BenchPairing::rehearse_local()',1)[1].split('bool BenchPairing::delete_local',1)[0]
        method='\n'.join(line.split('//',1)[0] for line in method.splitlines())
        for forbidden in ('send_pair(', 'Send ', 'disk.', 'send_frame(', 'pair_send(', '->verify(', 'exposure(',
                          'pair_charging_manager', 'pair_write', 'esp_ble_gattc', 'PRESENT_KEY',
                          'VcsecPacket', 'encode_', 'DomainAuthentication', 'session_info_tag'):
            self.assertNotIn(forbidden,method)
        shapes=source.split('struct LocalRehearsalWorkspace {',1)[1].split('\n};',1)[0]
        shapes=re.sub(r'sizeof\((Frame|VcsecPacket|DomainAuthentication)\)','SIZE_ONLY',shapes)
        for forbidden in ('Frame','VcsecPacket','DomainAuthentication','ChallengeTransport','encode_'):
            self.assertNotIn(forbidden,shapes)
        pair.BenchPairingTests().build_run(r'''
int main(){Fixture f;bool refuse=false;struct RandomContext{Fixture *f;bool *refuse;} ctx{&f,&refuse};
Random random{[](void *c,unsigned char *out,size_t n){auto &r=*static_cast<RandomContext *>(c);return *r.refuse?-1:mbedtls_ctr_drbg_random(&r.f->drbg,out,n);},&ctx};
BenchPairing p(f.disk,random);assert(!p.rehearse_local());assert(p.bind(f.vin,f.name,f.addr,1,true)&&p.prepare());
auto before=f.disk.record;auto key=std::string(p.public_key_hex());auto saves=f.disk.saves;
assert(p.rehearse_local());assert(!p.exposed()&&!p.active()&&!p.authenticated()&&!strcmp(p.state(),"key_ready"));
refuse=true;assert(!p.rehearse_local());
assert(before==f.disk.record&&key==p.public_key_hex()&&saves==f.disk.saves&&!f.disk.exposed);
}
''',forbidden_symbols=('pair_charging_manager(', 'BenchPairing::send_pair(', 'send_frame(',
                       'encode_session_request(', 'DomainAuthentication::begin(', 'session_info_tag('))
    def test_host_resources_but_no_scan_connection_subscription_or_write(self):
        source=(engine.MODULE/'tesla_ble_bench_discovery.cpp').read_text()
        self.assertIn('request_pair_rehearsal()',source)
        branch=source.split('if (!strcmp(report.action,"pair_rehearsal")) {',1)[1].split('\n                }',1)[0]
        self.assertIn('pairing->rehearse_local()',branch);self.assertIn('break;',branch)
        self.assertLess(source.index('if (!strcmp(report.action,"pair_rehearsal")) {'),source.index('portENTER_CRITICAL(&lock); accept_events = true;'))
        for forbidden in ('pair_transport(', 'send_pair(', 'send_frame(', 'pair_write(', 'esp_ble_gattc', 'encode_'):
            self.assertNotIn(forbidden,branch)
        harness=engine.HARNESS.replace('memcpy(p.scan_rst.ble_adv+5,"S0123456789abcdefC",18);','char vin[18]{},name[19];memset(vin,\'A\',17);assert(BenchPairing::vehicle_name(vin,name));memcpy(p.scan_rst.ble_adv+5,name,18);')
        main=r'''
int main(int argc,char **argv){auto &d=tesla_ble_bench_discovery;char vin[18]{};memset(vin,'A',17);
assert(d.request_pair_rehearsal()==409);assert(d.request_scan()==202);worker(context);assert(d.request_pair_prepare(0,vin)==202);worker(context);
calls.clear();if(argc>1)fail=argv[1];assert(d.request_pair_rehearsal()==202);assert(d.request_pair_start()==409&&d.request_pair_verify()==409&&d.request_pair_rehearsal()==409);worker(context);
auto s=d.snapshot();assert(!s.worker_active&&!s.transport_ready&&!s.request_may_have_reached_tesla&&s.local_key_present&&s.restart_required&&sent_wire.empty());
for(const auto &c:calls)assert(c.find("scan.")!=0&&c.find("gap.")!=0&&c.find("gatt.")!=0);
if(fail.empty()){assert(s.teardown_complete&&!s.failure_code&&!strcmp(s.pair_state,"key_ready"));for(const char *c:{"controller.init","controller.enable","host.init","host.enable","host.disable","host.deinit","controller.disable","controller.deinit"})assert(std::find(calls.begin(),calls.end(),c)!=calls.end());
TeslaBleBenchDiscovery::ResourceSample row;unsigned count=0;bool local=false,teardown=false;
while(d.resource_sample(count++,row)){assert(row.internal8_free==32768&&row.internal8_largest==16384&&row.internal8_min==32768&&row.stack_min_bytes==4096);local|=!strcmp(row.stage,"rehearsal.local");teardown|=!strcmp(row.stage,"host.deinit");}assert(count>10&&local&&teardown&&!s.resource_samples_dropped);}
else assert(s.failure_code==-77);
assert(d.request_pair_start()==409&&d.request_pair_verify()==409&&d.request_pair_rehearsal()==409);
}
'''
        with tempfile.TemporaryDirectory() as d:
            t=Path(d);engine.BenchDiscoveryTests().build(t,main=main,harness=harness)
            for scenario in ('','controller.init','controller.enable','host.init','host.enable','host.disable','host.deinit','controller.disable','controller.deinit'):
                for p in (t/'store').rglob('*'):
                    if p.is_file():p.unlink()
                result=subprocess.run([str(t/'test'),*([scenario] if scenario else [])],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)

if __name__=='__main__':unittest.main()
