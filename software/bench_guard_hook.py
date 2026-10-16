"""Revoke old bench success before frontend/build hooks can fail. No exports."""
from pathlib import Path
import sys

Import("env")

# SCons execs this script without defining __file__.
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "tools"))
from bench_review_export import review_paths
from bench_confinement import hook_cleanup

variant = {"warp4_tesla_ble_iram_bench": "ordinary",
           "warp4_tesla_ble_iram_bench_minimal": "minimal"}[env["PIOENV"]]
project = Path(env.subst("$PROJECT_DIR"))
targets = review_paths(project / ".pio/bench-exports" / variant)
hook_cleanup(project, Path(env.subst("$BUILD_DIR")), targets)
