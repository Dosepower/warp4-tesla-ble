"""Regression: real hook target selection and production tfpp freshness."""
import ast
from pathlib import Path
import tempfile
import unittest
import contextlib
import os
import shutil
import sys

S=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(S))

def hooks():
    # Import the actual hook definitions without executing SCons or main().
    tree=ast.parse((S/'pio_hooks.py').read_text())
    names={'preprocess_web','verify_preprocessed_web','build_web'}
    selected=ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[])
    import pathlib
    import tinkerforge_util as tfutil
    from web.tfpp import tfpp_paths,tfpp_lines
    namespace=dict(os=os,shutil=shutil,pathlib=pathlib,tfutil=tfutil,tfpp_paths=tfpp_paths,tfpp_lines=tfpp_lines,sys=sys,json=__import__('json'),hashlib=__import__('hashlib'))
    exec(compile(selected,str(S/'pio_hooks.py'),'exec'),namespace)
    return namespace

class FreshProductionWeb(unittest.TestCase):
    def test_actual_review_target_runs_full_generation(self):
        tree=ast.parse((S/'pio_hooks.py').read_text())
        main=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='main')
        assignment=next(n for n in main.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='build' for t in n.targets))
        expression=ast.Expression(assignment.value)
        for targets,expected in [([],True),(['tesla-review'],True),(['compiledb'],False),(['clean'],False),(['size'],True)]:
            self.assertEqual(eval(compile(expression,'hook-build-classification','eval'),{'COMMAND_LINE_TARGETS':targets}),expected,targets)

    def test_real_generator_and_stale_manual_entrypoint(self):
        h=hooks()
        with tempfile.TemporaryDirectory(prefix='tfpp-freshness-',dir=os.environ.get('TMPDIR',str(S/'.pio'))) as tmp:
            with contextlib.chdir(tmp):
                module=Path('web/src/modules/tesla_vehicle');module.mkdir(parents=True)
                (module/'main.tsx').write_text('//#include "flags.inc"\n//#if GUIDED\nexport {TeslaVehicle} from "./guided_vehicle";\n//#endif\n')
                (module/'flags.inc').write_text('//#define GUIDED 1\n')
                (module/'guided_vehicle.tsx').write_text('export const TeslaVehicle="fresh";\n')
                ignored=Path('web/src/modules/not_selected');ignored.mkdir();(ignored/'main.tsx').write_text('unselected')
                # Must ignore neither git-ignored main.tsx nor untracked guided component.
                Path('.gitignore').write_text('main.tsx\n')
                h['preprocess_web'](['tesla_vehicle'])
                self.assertTrue(Path('web/src_tfpp/modules/tesla_vehicle/guided_vehicle.tsx').is_file())
                self.assertFalse(Path('web/src_tfpp/modules/not_selected').exists())
                self.assertIn('verify_preprocessed_web',h,'Production must guard generator output, not source strings')
                h['verify_preprocessed_web']()
                entry=Path('web/src_tfpp/modules/tesla_vehicle/main.tsx')
                entry.write_text('export const TeslaVehicle="stale-manual-baseline";\n')
                with self.assertRaisesRegex(ValueError,'STALE_PREPROCESSED_WEB'):
                    h['build_web'](False,False,False)
                h['preprocess_web'](['tesla_vehicle']);h['verify_preprocessed_web']()
                # Includes/selection are evaluated by tfpp, not a string marker.
                (module/'flags.inc').write_text('//#define GUIDED 0\n')
                with self.assertRaisesRegex(ValueError,'STALE_PREPROCESSED_WEB'):h['verify_preprocessed_web']()
                h['preprocess_web'](['tesla_vehicle']);h['verify_preprocessed_web']()
                self.assertNotIn('export {TeslaVehicle}',entry.read_text())
                Path('web/src_tfpp/.tfpp-modules.json').unlink()
                with self.assertRaisesRegex(ValueError,'STALE_PREPROCESSED_WEB'):h['verify_preprocessed_web']()
                h['preprocess_web'](['tesla_vehicle']);h['verify_preprocessed_web']()
                (module/'guided_vehicle.tsx').write_text('export const TeslaVehicle="edited-after-generation";\n')
                with self.assertRaisesRegex(ValueError,'STALE_PREPROCESSED_WEB'):h['verify_preprocessed_web']()
                h['preprocess_web'](['tesla_vehicle']);h['verify_preprocessed_web']()
                Path('web/src_tfpp/modules/tesla_vehicle/foreign.tsx').write_text('stale orphan')
                with self.assertRaisesRegex(ValueError,'STALE_PREPROCESSED_WEB'):h['verify_preprocessed_web']()
                h['preprocess_web'](['tesla_vehicle']);entry.unlink()
                with self.assertRaisesRegex(ValueError,'STALE_PREPROCESSED_WEB'):h['verify_preprocessed_web']()

if __name__=='__main__':unittest.main()
