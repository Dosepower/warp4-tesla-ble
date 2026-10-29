"""Reviewed stage contract, not per-load provenance."""
import hashlib
import unittest
from installer_generation import Generation


def generation(files):
    return Generation(files, {"outputs": {n: {"size":len(d), "sha256":hashlib.sha256(d).hexdigest()}
                                           for n,d in files.items()}})


class StageMergeTests(unittest.TestCase):
    def test_multi_parent_merge_preserves_both_sealed_versions(self):
        import installer_generation as broker
        with generation({"a":b"one"}) as a, generation({"b":b"two"}) as b:
            merged = broker.merge_parents([a,b])
            self.assertEqual({n:g.read(n) for n,g in merged.items()}, {"a":b"one","b":b"two"})
            self.assertEqual(broker.merge_parents([a,a]), {"a":a})

    def test_conflicting_parent_versions_cannot_be_merged(self):
        import installer_generation as broker
        with generation({"a":b"one"}) as a, generation({"a":b"two"}) as b:
            with self.assertRaisesRegex(ValueError,"GENERATION_PARENT_CONFLICT"):
                broker.merge_parents([a,b])

    def test_parent_file_directory_conflict_is_rejected(self):
        import installer_generation as broker
        with generation({"web":b"one"}) as a, generation({"web/index":b"two"}) as b:
            with self.assertRaisesRegex(ValueError,"GENERATION_PARENT_CONFLICT"):
                broker.merge_parents([a,b])


import os
import tempfile
from pathlib import Path

@unittest.skipUnless(os.environ.get("WARP4_TEST_BWRAP"), "explicit Bubblewrap required")
class RealStageDAGTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from namespace_fixture import python_inputs
        from installer_snapshot import Snapshot
        cls.tmp = tempfile.TemporaryDirectory()
        project={"stage.py": b"import pathlib,sys\np=pathlib.Path('/output/web/assets');p.mkdir(parents=True)\np.joinpath('index').write_bytes(pathlib.Path('/generation/a').read_bytes()+pathlib.Path('/generation/b').read_bytes())\n",
                 "hang.py": b"while True: pass\n",
                 "oversize.py": b"with open('/output/result','wb') as f: f.seek(513*1024*1024); f.write(b'x')\n"}
        inputs,cls.binding=python_inputs(Path(cls.tmp.name),project)
        cls.snap=Snapshot(inputs)

    @classmethod
    def tearDownClass(cls):
        cls.snap.close(); cls.tmp.cleanup()

    def test_real_nested_web_output_and_multi_parent_record(self):
        from installer_generation import run_stage
        python="/snapshot/python/bin/python"; script="/snapshot/project/stage.py"
        with generation({"a":b"one"}) as a, generation({"b":b"two"}) as b:
            with run_stage(os.environ["WARP4_TEST_BWRAP"], self.snap,self.binding,
                           [python,"-I","-B",script],["web/assets/index"], parents=[a,b],
                           script_policy={script:{"interpreter":python,"arguments":["-I","-B"]}}) as result:
                self.assertEqual(result.read("web/assets/index"), b"onetwo")
                self.assertEqual(result.record["schema"],"warp4-stage-generation-v1")
                self.assertEqual(result.record["parents"],[a.identity,b.identity])
                self.assertNotIn("success",result.record)
                self.assertEqual(result.record["execution"]["script"]["sha256"],
                                 self.snap.manifest["files"][script]["sha256"])

    def test_named_materialization_runs_proxy_with_stable_self_exe(self):
        from installer_generation import run_stage
        from installer_snapshot import Snapshot
        from namespace_fixture import python_inputs
        import subprocess
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            source=root/"proxy.c"
            source.write_text('#include <unistd.h>\n#include <string.h>\n#include <stdio.h>\nint main(void){char p[4096]={0};if(readlink("/proc/self/exe",p,sizeof(p)-1)<0||strstr(p,"deleted"))return 9;FILE*f=fopen("/output/proxy-path","w");if(!f)return 10;fputs(p,f);return fclose(f);}')
            tool=root/"proxy"
            subprocess.run(["/usr/bin/cc","-static",str(source),"-o",str(tool)],check=True,capture_output=True)
            inputs,binding=python_inputs(root,{"installer_stage_entry.py":Path("installer_stage_entry.py").read_bytes() if Path("installer_stage_entry.py").exists() else b"missing stage entry"},native_tools={
                "/snapshot/proxy":tool,
                "/snapshot/system_tools/bwrap":Path(os.environ["WARP4_TEST_BWRAP"]),
                "/snapshot/system_tools/installer-materializer":Path(os.environ["WARP4_TEST_MATERIALIZER"])})
            for path in ("/snapshot/system_tools/bwrap","/snapshot/system_tools/installer-materializer"):
                inputs[path]["role"]="system_tools"
            with Snapshot(inputs) as snap:
                with run_stage(os.environ["WARP4_TEST_BWRAP"],snap,binding,
                               ["/snapshot/proxy"],["proxy-path"],named=True) as result:
                    self.assertEqual(result.read("proxy-path"),b"/snapshot/proxy")
                    self.assertEqual(result.record["execution"]["materialization"],"named-private-readonly")

    def test_host_dag_seals_ledger_without_accepting_build_json_or_unknown_parents(self):
        import installer_generation as broker
        self.assertTrue(hasattr(broker,'StageDAG'), 'host DAG ledger missing')
        python='/snapshot/python/bin/python';script='/snapshot/project/stage.py'
        with broker.StageDAG(os.environ['WARP4_TEST_BWRAP'],self.snap,self.binding) as dag:
            with self.assertRaisesRegex(ValueError,'STAGE_PARENT_UNKNOWN'):
                dag.run('web',[python,'-I','-B',script],['web/assets/index'],parents=['forged'])
            self.assertEqual(dag.evidence()['records'],[])
            # The API never accepts a caller-created Generation as a completed stage.
            with generation({'a':b'one'}) as untrusted:
                with self.assertRaisesRegex(ValueError,'STAGE_PARENT_UNKNOWN'):
                    dag.run('web',[python,'-I','-B',script],['web/assets/index'],parents=[untrusted])

    def test_unapproved_script_identity_fails_before_execution(self):
        from installer_generation import run_stage
        with self.assertRaisesRegex(ValueError,"GENERATION_SCRIPT_POLICY"):
            run_stage(os.environ["WARP4_TEST_BWRAP"],self.snap,self.binding,
                      ["/snapshot/python/bin/python","-I","-B","/snapshot/project/stage.py"],
                      ["result"],script_policy={})

    def test_inline_interpreter_code_has_no_immutable_script_identity(self):
        from installer_generation import run_stage
        with self.assertRaisesRegex(ValueError,'GENERATION_SCRIPT_POLICY'):
            run_stage(os.environ['WARP4_TEST_BWRAP'],self.snap,self.binding,
                      ['/snapshot/python/bin/python','-c',"open('/output/result','w').write('unbound')"],
                      ['result'],script_policy={})

    def test_stage_file_resource_limit_applies_before_output_promotion(self):
        import subprocess
        from installer_generation import run_stage
        python='/snapshot/python/bin/python';script='/snapshot/project/oversize.py'
        with self.assertRaises(subprocess.CalledProcessError):
            run_stage(os.environ['WARP4_TEST_BWRAP'],self.snap,self.binding,
                      [python,'-I','-B',script],['result'],
                      script_policy={script:{'interpreter':python,'arguments':['-I','-B']}})

    def test_timeout_does_not_promote_a_generation(self):
        import subprocess
        from installer_generation import run_stage
        python="/snapshot/python/bin/python"; script="/snapshot/project/hang.py"
        with self.assertRaises(subprocess.TimeoutExpired):
            run_stage(os.environ["WARP4_TEST_BWRAP"],self.snap,self.binding,
                      [python,"-I","-B",script],["result"],timeout=0.1,
                      script_policy={script:{"interpreter":python,"arguments":["-I","-B"]}})


class StageRecordTests(unittest.TestCase):
    def test_independent_verifier_rejects_untrusted_and_mutated_records(self):
        import installer_generation as broker
        self.assertTrue(hasattr(broker,"verify_stage_records"), "new stage-contract verifier missing")
        from installer_snapshot import canonical
        # This is a parser fixture only; production authority is the host-pinned head.
        record={"schema":"warp4-stage-generation-v1", "contract":"declared-action-boundary-not-actual-open-capture",
                "snapshot_sha256":"a"*64,"argv":["/snapshot/tool"],
                "input_scope":"conservative-exposure-not-observed-consumption",
                "execution":{"cwd":"/output","elf_binding":{"executables":["/snapshot/tool"],"interpreters":{},"libraries":{}},
                             "environment":{},"script":None,"materialization":"sealed-ro-bind-data"},
                "parent_generation":None,"parents":[],"outputs":{"x":{"size":1,"sha256":hashlib.sha256(b"x").hexdigest()}},
                "stdout_sha256":hashlib.sha256(b"").hexdigest()}
        digest=hashlib.sha256(canonical(record)).hexdigest()
        self.assertTrue(broker.verify_stage_records([record],"a"*64,[digest]))
        with self.assertRaisesRegex(ValueError,"STAGE_UNTRUSTED_HEAD"):
            broker.verify_stage_records([record],"a"*64,["b"*64])
        record["success"]=True
        with self.assertRaisesRegex(ValueError,"STAGE_RECORD_SCHEMA"):
            broker.verify_stage_records([record],"a"*64,[digest])
        del record['success']
        record['outputs']=['x']
        with self.assertRaisesRegex(ValueError,'STAGE_OUTPUT_SCHEMA'):
            broker.verify_stage_records([record],"a"*64,[digest])

    def test_independent_verifier_rejects_missing_parent_and_snapshot_change(self):
        import installer_generation as broker
        self.assertTrue(hasattr(broker,"verify_stage_records"), "new stage-contract verifier missing")
        from installer_snapshot import canonical
        record={"schema":"warp4-stage-generation-v1", "contract":"declared-action-boundary-not-actual-open-capture",
                "snapshot_sha256":"a"*64,"argv":["/snapshot/tool"],
                "input_scope":"conservative-exposure-not-observed-consumption",
                "execution":{"cwd":"/output","elf_binding":{"executables":["/snapshot/tool"],"interpreters":{},"libraries":{}},
                             "environment":{},"script":None,"materialization":"sealed-ro-bind-data"},
                "parent_generation":None,"parents":["b"*64],"outputs":{"x":{"size":1,"sha256":hashlib.sha256(b"x").hexdigest()}},
                "stdout_sha256":hashlib.sha256(b"").hexdigest()}
        digest=hashlib.sha256(canonical(record)).hexdigest()
        with self.assertRaisesRegex(ValueError,"STAGE_PARENT_NOT_SEALED"):
            broker.verify_stage_records([record],"a"*64,[digest])
        with self.assertRaisesRegex(ValueError,"STAGE_SNAPSHOT_MISMATCH"):
            broker.verify_stage_records([record],"c"*64,[digest])


class StagePairTests(unittest.TestCase):
    def test_stage_pair_uses_exposure_policy_and_independent_host_authority(self):
        import installer_stage_verify as verifier
        self.assertTrue(hasattr(verifier,'verify_stage_pair'), 'stage A/B verifier missing')
        from installer_snapshot import canonical
        def closure(tesla):
            common={'/snapshot/tool':'a'*64}
            manifest={'schema':'warp4-sealed-closure-v1','files':{p:{'sha256':h,'size':1,'mode':0o555,'role':'fixture'} for p,h in (common|tesla).items()}}
            snap=hashlib.sha256(canonical(manifest)).hexdigest()
            record={'schema':'warp4-stage-generation-v1','contract':'declared-action-boundary-not-actual-open-capture',
                    'snapshot_sha256':snap,'argv':['/snapshot/tool'],'input_scope':'conservative-exposure-not-observed-consumption',
                    'execution':{'cwd':'/output','elf_binding':{'executables':['/snapshot/tool'],'interpreters':{},'libraries':{}},
                                 'environment':{},'script':None,'materialization':'named-private-readonly'},
                    'parent_generation':None,'parents':[],'outputs':{'product':{'size':1,'sha256':'b'*64}},'stdout_sha256':'c'*64}
            heads=[hashlib.sha256(canonical(record)).hexdigest()]
            value={'schema':'warp4-stage-closure-v1','common':common,'tesla':tesla,'manifest':manifest,'records':[record]}
            return value,{'snapshot_sha256':snap,'heads':heads}
        delta={'/snapshot/tesla.cpp':'d'*64}
        a,aa=closure({});b,ba=closure(delta)
        self.assertTrue(verifier.verify_stage_pair(a,b,delta,[aa,ba]))
        from installer_link_verify import verify_pair
        from installer_policy import ENVS
        manifests=[]
        for name,closure in zip(ENVS,(a,b)):
            provenance={k:'f'*64 for k in ('sdk_manifest_sha256','sdk_inventory_sha256','runtime_manifest_sha256','public_key_sha256')}
            provenance['environment']=name
            manifests.append({'verification':{'product_id':'warp4'},'provenance':provenance,'stage_closure':closure})
        self.assertTrue(verify_pair(*manifests,approved_tesla=delta,stage_authorities=[aa,ba]))
        with self.assertRaisesRegex(ValueError,'STAGE_PAIR_AUTHORITY'):
            verify_pair(*manifests,approved_tesla=delta)

        # Exposure supersets are NOT required to be consumed exactly.
        self.assertEqual(a['records'][0]['argv'],['/snapshot/tool'])
        with self.assertRaisesRegex(ValueError,'STAGE_PAIR_AUTHORITY'):
            verifier.verify_stage_pair(a,b,delta,None)
        with self.assertRaisesRegex(ValueError,'AB_TESLA_DELTA_DIFFERS'):
            verifier.verify_stage_pair(a,b,{},[aa,ba])
        ba['heads']=['e'*64]
        with self.assertRaisesRegex(ValueError,'STAGE_UNTRUSTED_HEAD'):
            verifier.verify_stage_pair(a,b,delta,[aa,ba])
