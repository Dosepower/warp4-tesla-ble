"""Execute the production reader after an admitted identity/config replacement.
No sanitizer is used as a proof of race freedom: mutable config is a poison pill.
"""
from pathlib import Path
import os, subprocess, tempfile, unittest
from test_iram_permanent_allocation import function
S=Path(__file__).resolve().parents[2]
class ProfileSnapshot(unittest.TestCase):
    def test_admitted_reader_never_touches_replaced_config(self):
        reader=function((S/'src/modules/ev/ev.cpp').read_text(), 'uint64_t Ev::get_profile_mac(int index) const')
        reader=reader.replace('Ev::get_profile_mac', 'get_profile_mac')
        publisher=function((S/'src/modules/ev/ev.cpp').read_text(), 'void Ev::publish_profile_macs(const Config &update)').replace('Ev::publish_profile_macs','publish_profile_macs')
        parser=function((S/'src/modules/ev/ev.cpp').read_text(), 'static bool parse_mac(')
        code=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#define ASSERT_MAIN_THREAD() assert(!worker)
constexpr int EV_MAX_EVS=16,EV_MAC_ADDRESS_LENGTH=6,EV_MAC_STRING_LENGTH=17;
bool worker=false;
struct Config {
 std::string text;
 std::vector<Config> profiles;
 const Config *get(const char *) const { assert(!worker && "worker traversed replaced mutable config");return this; }
 const Config *get(unsigned i) const { return &profiles.at(i); }
 unsigned count() const { return profiles.size(); }
 const std::string &asString() const { return text; }
};
''' + parser + r'''
struct Ev {
 Config config;
 std::atomic<uint32_t> profile_revision{0};
 std::atomic<uint64_t> profile_macs[EV_MAX_EVS]{};
''' + reader + publisher + r'''
};
int main() {
 Ev ev;constexpr uint64_t old_mac=0x112233445500ULL,new_mac=old_mac+1;
 ev.config.profiles.push_back({"11:22:33:44:55:00",{}});
 ev.publish_profile_macs(ev.config);
 worker=true;assert(ev.get_profile_mac(0)==old_mac);
 // Worker admitted the old LiveIdentity, then main publishes the replacement
 // and destroys the old vector/string storage before that reader's MAC check.
 worker=false;Config replacement;replacement.profiles.push_back({"11:22:33:44:55:01",{}});
 ev.publish_profile_macs(replacement);ev.config=std::move(replacement);
 worker=true;assert(ev.get_profile_mac(0)==new_mac && ev.get_profile_mac(0)!=old_mac);
 ev.profile_revision=3;
 assert(ev.get_profile_mac(0)==0); // Publishing is fail closed.
 ev.profile_revision=4;
 assert(ev.get_profile_mac(0)==new_mac);
 assert(ev.get_profile_mac(-1)==0 && ev.get_profile_mac(EV_MAX_EVS)==0);
 assert(ev.get_profile_mac(1)==0);
 worker=false;ev.config.profiles[0].text="11:22:33:44:55:xx";ev.publish_profile_macs(ev.config);
 worker=true;assert(ev.get_profile_mac(0)==0); // wildcard never grants Tesla authority
 worker=false;ev.config.profiles.clear();ev.publish_profile_macs(ev.config);
 worker=true;assert(ev.get_profile_mac(0)==0); // removal clears the snapshot
}
'''
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
    def test_authoritative_publication_precedes_config_replacement(self):
        source=(S/'src/modules/ev/ev.cpp').read_text()
        self.assertIn('publish_profile_macs(update)', source)
        self.assertIn('publish_profile_macs(config)', source)
        body=function(source,'void Ev::publish_profile_macs(const Config &update)')
        self.assertIn('profile_revision.fetch_add(1)',body)
        self.assertIn('profile_macs[i].store',body)
        self.assertIn('ASSERT_MAIN_THREAD()',body)
if __name__=='__main__':unittest.main()
