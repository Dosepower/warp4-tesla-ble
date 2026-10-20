"""Compile actual ISO policy/classification methods, not source-text decisions."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from test_iram_permanent_allocation import function

S = Path(__file__).resolve().parents[2]
SAN = 'sanitize' in sys.argv
if SAN: sys.argv.remove('sanitize')

class PLCSafety(unittest.TestCase):
    def test_compiled_cable_policy(self):
        header = (S/'src/modules/iso15118/iso15118.h').read_text()
        common = (S/'src/modules/iso15118/common.cpp').read_text()
        enum = (S/'src/modules/iso15118/EVCC Vendor.uint8.enum').read_text()
        vendors = [line.strip().replace(' ', '') for line in enum.splitlines() if line.strip()]
        table = common[common.index('struct EVCCVendorOUI'):common.index('void Common::set_evcc_vendor')]
        methods = '\n'.join(function(header, name) for name in (
            'bool is_soc_compatible()', 'bool is_autocharge_only()',
            'bool is_read_soc_only()', 'bool is_autocharge()'))
        methods += function(header, 'bool is_slac_only()')
        detect = function(common, 'void Common::detect_evcc_vendor_from_mac').replace('Common::', '')
        common_header = (S/'src/modules/iso15118/common.h').read_text()
        reset = function(common_header, 'void reset_evcc_vendor()')
        identity_fields = common_header.split('private:', 1)[1].split('    void handle_session_setup_req();', 1)[0]
        slac = (S/'src/modules/iso15118/slac.cpp').read_text()
        routing = slac[slac.index('#if MODULE_EV_AVAILABLE()', slac.index('log_cm_slac_match_request(cm_slac_match_request);')):slac.index('    CM_SLACMatchConfirmation cm_slac_match_confirmation = {};')]
        auth = function((S/'src/modules/ev/ev.cpp').read_text(), 'if (iso15118.is_autocharge())')
        code = '''#include <cassert>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>
#define OPTIONS_ISO15118_ENABLE_TESTING_OPTIONS() 1
enum class EVCCVendor {''' + ','.join(vendors) + '''};
''' + table + '''
struct Common {
 EVCCVendor vendor=EVCCVendor::Unknown;
 void set_evcc_vendor(EVCCVendor v) {vendor=v;}
 EVCCVendor get_evcc_vendor() const {return vendor;}
''' + identity_fields + detect + reset + '''};
struct Value { bool v=false; bool asBool() const {return v;} };
struct Config { std::map<std::string,Value> values;
 const Value *get(const char *k) const {return &values.at(k);}
};
struct ISO15118 { Common common;
 Config config{{{"autocharge",{true}},{"read_soc",{true}},{"charge_via_iso15118",{false}}}};
 Config config_experimental{{{"ignore_soc_compatibility",{false}}}};
''' + methods + '''
 enum class ModemOff {Immediate};
 bool nonegotiation_pending=false, noneg=false; int reslac_guard_deadline=0, transitions=0;
 bool use_nonegotiation_autocharge() const {return noneg;}
 void begin_iec_transition(ModemOff) {++transitions;}
 template<class... A> void trace(A...) {}
} iso15118;
constexpr int operator""_us(unsigned long long v) {return v;}
constexpr int operator""_ms(unsigned long long v) {return v;}
enum class CMAuthType {InjectedEV,EV};
struct Auth {int calls=0; template<class... A> void notify_auth(A...) {++calls;}} charge_authorization;
struct Info {int value=0; Info *get(const char *) {return this;} void updateString(const char *) {}};
struct EV {int observations=0; Info auth_info;
 int get_user_id(const uint8_t *) {return 0;}
 void on_ev_connected(const uint8_t *mac) {++observations; bool injected=false; const char *mac_fmt="synthetic";
''' + auth + '''
 }
} ev;
#define MODULE_EV_AVAILABLE() 1
constexpr int SLAC_MAC_ADDRESS_LENGTH=6;
enum class SLACState {LinkDetected};
struct SLAC {bool ev_connected_reported=false; uint8_t reported_pev_mac[6]{},pev_mac[6]{};
 int next_timeout=0, v2g_entries=0; SLACState state{};
 void match() {
''' + routing + '''
 ++v2g_entries; // boundary: CM_SLAC_MATCH.CNF / SDP follows this real early return
 }
};
int main(int argc,char **argv) {
 assert(argc==2); std::string mode=argv[1]; auto &p=iso15118;
 uint8_t tesla[]={0x0c,0x29,0x8f,1,2,3}, bmw[]={0,1,0xa9,1,2,3}, unknown[]={2,2,2,1,2,3};
 p.common.detect_evcc_vendor_from_mac(tesla);
 assert(p.common.vendor==EVCCVendor::Tesla);
 if(mode=="tesla-auto") { assert(p.is_autocharge()); assert(p.is_autocharge_only()); assert(p.is_slac_only()); assert(!p.is_read_soc_only()); }
 else if(mode=="tesla-no-auto") {p.config.values["autocharge"].v=false; assert(!p.is_autocharge()); assert(p.is_slac_only()); assert(!p.is_read_soc_only());}
 else if(mode=="tesla-override") {p.config_experimental.values["ignore_soc_compatibility"].v=true; assert(!p.is_soc_compatible()); assert(p.is_slac_only());}
 else if(mode=="non-tesla") {p.common.reset_evcc_vendor(); p.common.detect_evcc_vendor_from_mac(bmw); assert(p.common.vendor==EVCCVendor::BMW); assert(!p.is_slac_only()); assert(p.is_read_soc_only());}
 else if(mode=="new-unknown") {p.common.detect_evcc_vendor_from_mac(unknown); assert(p.common.vendor==EVCCVendor::Unknown); assert(!p.is_slac_only()); assert(p.is_read_soc_only());}
 else if(mode=="all-ouis") { for(auto &e:evcc_vendor_ouis) {uint8_t mac[]={e.oui[0],e.oui[1],e.oui[2],1,2,3}; p.common.detect_evcc_vendor_from_mac(mac); assert(p.common.vendor==e.vendor); assert(p.is_slac_only()==(e.vendor==EVCCVendor::Tesla));} }
 else if(mode=="tesla-noneg") {p.noneg=true;}
 else if(mode=="tesla-iso-charge") {p.config.values["charge_via_iso15118"].v=true; assert(p.is_slac_only());}
 else assert(false);
 SLAC s; memcpy(s.pev_mac,tesla,6); s.match();
 const bool is_tesla=p.common.vendor==EVCCVendor::Tesla;
 assert(ev.observations==1);
 assert(charge_authorization.calls==(p.is_autocharge()?1:0));
 assert(s.v2g_entries==(is_tesla?0:1));
 assert(p.transitions==(is_tesla?1:0));
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            d=Path(td)
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie'] if SAN else []
            for testing in (0, 1):
                (d/'test.cpp').write_text(code.replace('#define OPTIONS_ISO15118_ENABLE_TESTING_OPTIONS() 1',
                                                       f'#define OPTIONS_ISO15118_ENABLE_TESTING_OPTIONS() {testing}'))
                subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,str(d/'test.cpp'),'-o',str(d/'test')],check=True)
                for mode in ('tesla-auto','tesla-no-auto','tesla-override','non-tesla','new-unknown','all-ouis','tesla-noneg','tesla-iso-charge'):
                    with self.subTest(testing=testing, mode=mode):
                        subprocess.run([str(d/'test'),mode],check=True)
                        print(f'PLC_SAFETY testing={testing} {mode} PASS', flush=True)

if __name__=='__main__': unittest.main()
