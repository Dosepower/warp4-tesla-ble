"""Production stage boundaries. Synthetic authority is test-only."""
import copy
import hashlib
import unittest
from installer_snapshot import canonical

class ProductionStageTests(unittest.TestCase):
    def test_producer_requires_explicit_common_delta_and_shared_key_sdk_authority(self):
        from installer_recipe import stage_inputs
        inputs={"/snapshot/project/base": {"sha256":"a"*64,"role":"source"},
                "/snapshot/project/tesla_vehicle.cpp":{"sha256":"b"*64,"role":"source"},
                "/snapshot/sdk/lib.a":{"sha256":"c"*64,"role":"sdk"},
                "/snapshot/project/signature/key":{"sha256":"d"*64,"role":"official_identity"}}
        common={p:v["sha256"] for p,v in inputs.items() if "tesla_vehicle" not in p}
        delta={"/snapshot/project/tesla_vehicle.cpp":"b"*64}
        policy={"schema":"warp4-production-stage-policy-v1","common":common,"tesla":delta}
        a,b=stage_inputs(inputs,policy)
        self.assertEqual(set(a),set(common))
        self.assertEqual(b,inputs)
        for kind,path in (("common","/snapshot/sdk/lib.a"),("common","/snapshot/project/signature/key"),("tesla","/snapshot/project/tesla_vehicle.cpp")):
            bad=copy.deepcopy(policy);bad[kind][path]="f"*64
            with self.assertRaisesRegex(ValueError,"STAGE_PRODUCER"):
                stage_inputs(inputs,bad)
        bad=copy.deepcopy(policy);bad["tesla"]["/snapshot/sdk/lib.a"]=bad["common"].pop("/snapshot/sdk/lib.a")
        with self.assertRaisesRegex(ValueError,"STAGE_PRODUCER"):
            stage_inputs(inputs,bad)

    def test_stage_namespace_transports_only_sealed_host_context(self):
        import inspect
        from installer_generation import run_stage
        self.assertIn("context_fd",inspect.signature(run_stage).parameters,
                      "production broker cannot transport readonly host stage authority")

    def test_posthook_requires_host_owned_stage_parent_not_output_ledger(self):
        from installer_stage_verify import verify_production_stage
        with self.assertRaisesRegex(ValueError, 'PRODUCTION_STAGE'):
            verify_production_stage({}, b'{}')
