"""Installer generator selection, not origin approval of local dependencies."""
import unittest

class GeneratorTests(unittest.TestCase):
    def test_uv_and_npx_are_replaced_by_fixed_snapshot_consumers(self):
        from installer_generators import command
        records={"/snapshot/project/src/modules/x/prepare.py":{"role":"source"},
                 "/snapshot/project/installer_python_entry.py":{"role":"source"},
                 "/snapshot/project/web/node_modules/typescript/bin/tsc":{"role":"web_dependencies"},
                 "/snapshot/python/bin/python":{"role":"python_environment"},
                 "/snapshot/node_environment/bin/node":{"role":"node_environment"}}
        self.assertEqual(command(["uv","run","prepare.py"],"/work/project/src/modules/x","warp4_ble_installer",records),
                         ["/snapshot/python/bin/python","-I","-B","/snapshot/project/installer_python_entry.py","warp4_ble_installer","/work/project/src/modules/x/prepare.py"])
        self.assertEqual(command(["uv","run","npx","tsc","--build","tsconfig.json"],"/work/project/web/src","warp4_ble_installer",records),
                         ["/snapshot/node_environment/bin/node","/snapshot/project/web/node_modules/typescript/bin/tsc","--build","tsconfig.json"])
        for argv in (["uv","run","npm","ci"],["uv","run","nodeenv"],["uv","run","../host.py"],["sh","-c","bad"],["uv","run","npx","unapproved"]):
            with self.subTest(argv=argv), self.assertRaises(ValueError):
                command(argv,"/work/project","warp4_ble_installer",records)

    def test_hook_dispatches_only_installer_branch_through_snapshot_context(self):
        import ast
        from pathlib import Path
        from unittest.mock import Mock, patch, MagicMock
        import installer_generators
        tree=ast.parse((Path(__file__).resolve().parents[2]/"pio_hooks.py").read_text())
        call=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=="check_call")
        import subprocess,sys
        env=Mock(); env.GetProjectOption.return_value="true"
        env.__getitem__=Mock(side_effect=lambda k: {"PIOENV":"warp4_ble_installer","WARP4_INSTALLER_VERIFIED":{"fixture":True}}[k])
        scope={"env":env,"subprocess":subprocess,"sys":sys}
        exec(compile(ast.Module(body=[call],type_ignores=[]),"check-call-only","exec"),scope)
        with patch.object(installer_generators,"run") as run, patch.object(subprocess,"check_call") as host:
            scope["check_call"](["uv","run","prepare.py"])
            run.assert_called_once()
            host.assert_not_called()

    def test_fresh_installer_web_uses_frozen_dependencies_without_npm_install(self):
        import ast
        from pathlib import Path
        from unittest.mock import Mock, patch, MagicMock
        import installer_generators
        tree=ast.parse((Path(__file__).resolve().parents[2]/"pio_hooks.py").read_text())
        main=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=="main")
        start=next(i for i,n in enumerate(main.body) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Tuple) and any(isinstance(x,ast.Name) and x.id=="node_modules_needs_update" for x in t.elts) for t in n.targets))
        end=next(i for i,n in enumerate(main.body[start:],start) if isinstance(n,ast.Expr) and isinstance(n.value,ast.Call) and n.value.args and isinstance(n.value.args[0],ast.Constant) and n.value.args[0].value=="Checking web interface")
        env=Mock(); env.__getitem__=Mock(side_effect=lambda k:{"PIOENV":"warp4_ble_installer","WARP4_INSTALLER_VERIFIED":{"fixture":True}}[k])
        util=Mock(); util.check_digest.return_value=(True,"no stale cache","digest")
        call=Mock(side_effect=AssertionError("installer must not provision dependencies"))
        scope={"env":env,"util":util,"installer_ota_only":True,"node_modules_src_paths":[],"node_modules_src_datas":[],"os":MagicMock(),"check_call":call,"shutil":MagicMock(),"time":Mock(),"tfutil":MagicMock(),"sys":__import__("sys"),"requires_node":"fixture","requires_npm":"fixture","subprocess":MagicMock()}
        scope["shutil"].which.return_value=None
        scope["os"].path.exists.return_value=False
        # No real filesystem or process operations in an extracted hook block.
        with patch.object(installer_generators,"verify_frozen_web") as frozen:
            exec(compile(ast.Module(body=main.body[start:end],type_ignores=[]),"web-dependencies-only","exec"),scope)
            frozen.assert_called_once_with("warp4_ble_installer",{"fixture":True})
        call.assert_not_called()

    def test_installer_git_labels_are_explicit_approved_snapshot_metadata(self):
        import installer_generators as generators
        self.assertTrue(hasattr(generators,'source_metadata'), 'snapshot source metadata missing')
        from unittest.mock import patch
        source={'is_release':False,'is_dirty':True,'git_url':'https://example.invalid/synthetic-fixture',
                'git_commit_id':'a'*15,'branch_name':'fixture'}
        guarded={'source_metadata':source,'source_head':'a'*40}
        with patch.object(generators,'read_build_context',return_value=guarded):
            self.assertEqual(generators.source_metadata('warp4_ble_installer',guarded),source)
            guarded['source_metadata']=source | {'git_commit_id':'b'*15}
            with self.assertRaisesRegex(ValueError,'GENERATOR_SOURCE_METADATA'):
                generators.source_metadata('warp4_ble_installer',guarded)
