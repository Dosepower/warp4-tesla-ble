#!/usr/bin/env python3
"""Operation stack gate must reject unknowns, recursion and insufficient margin."""
from pathlib import Path
import importlib.util,unittest,tempfile,os,json,hashlib
ROOT=Path(__file__).resolve().parents[3]
TOOL='/home/hermes/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-'
def audit_module():
    spec=importlib.util.spec_from_file_location('operation_stack',ROOT/'software/tools/bench_operation_stack.py')
    assert spec is not None and spec.loader is not None
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
class OperationStackGateTests(unittest.TestCase):
    @classmethod
    def linked_graph(cls):
        if hasattr(cls,'_graph'):return cls._graph
        elf=Path(os.environ.get('TESLA_BLE_HEAP_TEST_ELF',ROOT/'software/.pio/build/warp4_tesla_ble_iram_bench/firmware.elf'))
        with tempfile.TemporaryDirectory() as directory:
            cls._graph,_,_=audit_module().linked_graph(elf,Path(directory),TOOL)
        return cls._graph
    def test_linked_rehearsal_forbidden_symbols_are_unreachable(self):
        graph=self.linked_graph();root='BenchPairing::rehearse_local()'
        owners=[n for n in graph if n.startswith('BluetoothLifecycle::run(')]
        if owners:
            # Current direct-IDF lifecycle has no rehearsal operation. Production
            # Tesla crypto IS reachable and must not be confused with rehearsal.
            self.assertEqual(len(owners),1)
            self.assertFalse([n for n in graph if n.startswith(('BenchPairing::','BenchPairDisk::','TeslaBleBenchDiscovery::','TeslaBleBenchSmoke::','tesla_radio_spike','nimble_port_','ble_gap_','ble_gattc_','net_buf_'))])
            row=graph[owners[0]]
            self.assertTrue(row['coverage_complete'],row)
            self.assertFalse(row['unknown'],row)
            self.assertFalse(row['indirect_calls'],row)
            self.assertFalse([n for n in row['calls'] if n.startswith(('esp_ble_gap_start_scanning','esp_ble_gap_start_advertising','esp_ble_gattc_open'))])
            callback=next(n for n in graph if n.startswith('BluetoothLifecycle::gatt('))
            self.assertTrue(graph[callback]['coverage_complete'])
            # The application callback remains an unresolved edge, not zero cost.
            self.assertEqual(len(graph[callback]['indirect_calls']),1)
            self.assertTrue(graph[callback]['unknown'])
            return
        if 'BluetoothMasterBench::run()' in graph:
            # The R13 master image deliberately removes, rather than exercises,
            # all legacy rehearsal/pairing operations. Assert absence, not skip.
            self.assertFalse([n for n in graph if n.startswith(('BenchPairing::','BenchPairDisk::','TeslaBleBenchDiscovery::','TeslaBleBenchSmoke::'))])
            self.assertFalse([n for n in graph if n.startswith('tesla_clean::') and '::~' not in n])
            self.assertFalse([n for n in graph if n.startswith('net_buf_')])
            return
        row=audit_module().rehearsal_reachability(graph,root)
        self.assertFalse(row['forbidden_reachable'],row)
        self.assertTrue(graph[root]['coverage_complete'],graph[root])
        for name in row['reachable_symbols']:
            if name.startswith('tesla_clean::') and name in graph:
                self.assertTrue(graph[name].get('coverage_complete',True),(name,graph[name]))
        # Local helper must be fully decoded, with the configured Random.fill
        # as its only unresolved application call site (vendor stack is open).
        self.assertEqual(len(graph[root]['indirect_calls']),1,graph[root])
        self.assertFalse([name for name in graph if name.startswith('net_buf_')], 'Unused Mesh must not link in this bench image')

    def test_action_budgets_are_exact_and_elf_bound(self):
        module=audit_module()
        with tempfile.TemporaryDirectory() as directory:
            header=Path(directory)/'budget.h';manifest=Path(directory)/'inputs.json'
            header.write_text('PAIR_WORKER_BYTES=8192; REHEARSAL_WORKER_BYTES=6144; DISCOVERY_WORKER_BYTES=4096;')
            inputs={'files':{str(header.resolve()):hashlib.sha256(header.read_bytes()).hexdigest()}}
            manifest.write_text(json.dumps(inputs))
            nm='00000000 A bench_inputs_'+hashlib.sha256(json.dumps(inputs,sort_keys=True,separators=(',',':')).encode()).hexdigest()
            constants,evidence=module.bound_worker_constants(header,manifest,nm)
            self.assertEqual(constants['REHEARSAL_WORKER_BYTES'],6144);self.assertEqual(constants['PAIR_WORKER_BYTES'],8192)
            graph={'work':{'frame':5000,'calls':[],'unknown':[]}}
            row=module.bound(graph,['work'],288,constants['REHEARSAL_WORKER_BYTES'])
            self.assertEqual(row['worker_bytes'],6144);self.assertEqual(row['remaining_bytes'],856);self.assertFalse(row['margin_at_least_1024'])
            header.write_text(header.read_text().replace('6144','8192'))
            with self.assertRaisesRegex(ValueError,'differs from compiled input'):module.bound_worker_constants(header,manifest,nm)
            with self.assertRaisesRegex(ValueError,'not bound to exact ELF'):module.bound_worker_constants(header,manifest,'')
    def test_linked_known_critical_edges_are_not_silently_omitted(self):
        graph=self.linked_graph()
        if any(n.startswith('BluetoothLifecycle::run(') for n in graph):
            prefix='__wrap_' if '__wrap_esp_bt_controller_init' in graph else ''
            expected={
                'BluetoothLifecycle::run(':['BluetoothLifecycle::teardown(',
                    *[prefix+n for n in ('esp_bt_controller_init','esp_bt_controller_enable','esp_bluedroid_init','esp_bluedroid_enable','esp_ble_gattc_register_callback','esp_ble_gattc_app_register')]],
                'BluetoothLifecycle::teardown(':[prefix+n for n in ('esp_bluedroid_disable','esp_bluedroid_deinit','esp_bt_controller_disable','esp_bt_controller_deinit')],
            }
            for caller,callees in expected.items():
                name=next(n for n in graph if n.startswith(caller))
                self.assertTrue(graph[name]['coverage_complete'],(caller,graph[name]))
                self.assertFalse(graph[name]['unknown'],(caller,graph[name]))
                for callee in callees:
                    self.assertTrue(any(c.startswith(callee) for c in graph[name]['calls']),(caller,callee,graph[name]))
            return
        if 'BluetoothMasterBench::run()' in graph:
            expected={
                'BluetoothMasterBench::run()':['BLEDevice::init(', '__wrap_esp_ble_gattc_app_register', 'BluetoothMasterBench::teardown('],
                'BluetoothMasterBench::teardown(':['BLEDevice::deinit('],
                'BLEDevice::deinit(':['__wrap_esp_bluedroid_disable', '__wrap_esp_bluedroid_deinit',
                                     '__wrap_esp_bt_controller_disable', '__wrap_esp_bt_controller_deinit'],
            }
            for caller,callees in expected.items():
                name=next(n for n in graph if n.startswith(caller))
                self.assertTrue(graph[name]['coverage_complete'],(caller,graph[name]))
                for callee in callees:
                    self.assertTrue(any(c.startswith(callee) for c in graph[name]['calls']),(caller,callee,graph[name]))
            return
        expected={
            'BenchPairing::verify(':['tesla_clean::DomainAuthentication::begin('],
            'BenchPairing::prepare()':['tesla_clean::IdentityStore::rotate(', 'tesla_clean::IdentityStore::import_pem('],
            'TeslaBleBenchDiscovery::pair_transport()':['BenchPairing::send_pair(', 'BenchPairing::verify('],
            'tesla_clean::P256Key::exchange(':['mbedtls_ecdh_compute_shared'],
            'BenchPairing::rehearse_local()':['tesla_clean::P256Key::exchange(', 'mbedtls_md_hmac'],
            'BenchPairing::send_pair(':['tesla_clean::pair_charging_manager('],
            'BenchPairing::receive_frame(':['tesla_clean::DomainAuthentication::receive('],
            'tesla_clean::DomainAuthentication::receive(':['tesla_clean::authenticate_session_info('],
            'tesla_clean::authenticate_session_info(':['tesla_clean::P256Key::exchange(', 'tesla_clean::SessionCrypto::session_info_tag('],
            'tesla_clean::SessionCrypto::session_info_tag(':['mbedtls_md_hmac'],
        }
        source=(ROOT/'software/src/modules/tesla_vehicle/tesla_ble_bench_pairing.cpp').read_text().split('bool BenchPairing::rehearse_local()',1)[1].split('bool BenchPairing::delete_local',1)[0]
        if '->begin(' in source:expected['BenchPairing::rehearse_local()'].append('tesla_clean::DomainAuthentication::begin(')
        for caller,callees in expected.items():
            name=next(n for n in graph if n.startswith(caller))
            self.assertTrue(graph[name]['coverage_complete'],(caller,graph[name]))
            for callee in callees:self.assertTrue(any(c.startswith(callee) for c in graph[name]['calls']),(caller,callee,graph[name]))
    def test_missing_reachable_decode_is_coverage_failure(self):
        module=audit_module()
        self.assertTrue(hasattr(module,'walk_blocks'),'need actual-address block decoder')
        decoded={0x1000:{'size':3,'op':'entry','arg':'a1, 32'},0x1003:{'size':3,'op':'beqz','arg':'a2, 1009 <f+0x9>'},0x1006:{'size':3,'op':'retw','arg':''}}
        row=module.walk_blocks(0x1000,0x100c,lambda pc:decoded.get(pc))
        self.assertFalse(row['coverage_complete']);self.assertIn(0x1009,row['coverage_failures'])
        # Explicit branch decoding must recover the destination, and never walk
        # sequentially across literal/data or alignment gaps after a return.
        decoded[0x1009]={'size':3,'op':'retw','arg':''}
        row=module.walk_blocks(0x1000,0x100c,lambda pc:decoded.get(pc))
        self.assertTrue(row['coverage_complete']);self.assertEqual(set(row['reachable']),set(decoded))
        # Do not start at "first available instruction" if entry is absent.
        row=module.walk_blocks(0x0ffd,0x100c,lambda pc:decoded.get(pc))
        self.assertFalse(row['coverage_complete']);self.assertEqual(row['coverage_failures'],[0x0ffd])
        # A reachable data word cannot be skipped to the next decoded opcode.
        decoded.pop(0x1009)
        self.assertFalse(module.walk_blocks(0x1000,0x100c,lambda pc:decoded.get(pc))['coverage_complete'])
        # A proven noreturn CALL terminates only the caller's fallthrough. Its
        # fatal callee still contributes to the callgraph/stack audit.
        decoded={0x1000:{'size':3,'op':'call8','arg':'2000 <__stack_chk_fail>','noreturn':True}}
        self.assertTrue(module.walk_blocks(0x1000,0x1004,lambda pc:decoded.get(pc))['coverage_complete'])
        graph={'caller':{'frame':32,'calls':['__stack_chk_fail'],'unknown':[]},'__stack_chk_fail':{'frame':48,'calls':[],'unknown':['fatal vendor work unbounded']}}
        self.assertFalse(module.bound(graph,['caller'],288,8192)['complete'])
    def test_unknown_is_not_zero_and_margin_is_hard_gate(self):
        path=ROOT/'software/tools/bench_operation_stack.py'
        self.assertTrue(path.is_file(),'operation-specific linked audit is required')
        spec=importlib.util.spec_from_file_location('operation_stack',path)
        assert spec is not None and spec.loader is not None
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        graph={'entry':{'frame':32,'calls':['work'],'unknown':[],'dynamic':False},'work':{'frame':6800,'calls':[],'unknown':[],'dynamic':False}}
        row=module.bound(graph,['entry'],288,8192)
        self.assertTrue(row['complete']);self.assertTrue(row['margin_at_least_1024']);self.assertEqual(row['bounded_bytes'],7120)
        graph['work']['frame']=7000;row=module.bound(graph,['entry'],288,8192)
        self.assertTrue(row['complete']);self.assertFalse(row['margin_at_least_1024'])
        graph['work']['unknown']=['finite target not resolved'];row=module.bound(graph,['entry'],288,6144)
        self.assertFalse(row['complete']);self.assertFalse(row['margin_at_least_1024']);self.assertIsNone(row['bounded_bytes'])
        graph['work']['unknown']=[];graph['work']['calls']=['entry'];row=module.bound(graph,['entry'],288,8192)
        self.assertFalse(row['complete']);self.assertTrue(row['cycles'])
        graph['work']['calls']=['missing'];self.assertFalse(module.bound(graph,['entry'],288,8192)['complete'])
        graph['work']['calls']=[];graph['work']['dynamic']=True;self.assertFalse(module.bound(graph,['entry'],288,8192)['complete'])
        graph['work']['dynamic']=False;graph['work']['coverage_complete']=False
        self.assertFalse(module.bound(graph,['entry'],288,8192)['complete'])
        graph['work']['coverage_complete']=True;graph['work']['frame']=5000
        small=module.bound(graph,['entry'],288,6144);large=module.bound(graph,['entry'],288,8192)
        self.assertEqual(small['worker_bytes'],6144);self.assertFalse(small['margin_at_least_1024']);self.assertTrue(large['margin_at_least_1024'])
if __name__=='__main__':unittest.main()
