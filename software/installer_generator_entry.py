"""Fixed complete WARP generator entry; no SDK provisioning or compilation."""
import sys
sys.path.insert(0, '/snapshot/project')
from installer_generators import generate
from installer_sdk_verify import require
require(len(sys.argv)==2, 'GENERATOR_ARGUMENTS_REQUIRED')
generate(sys.argv[1])
