"""Executable production Common classification, negotiation and decode routing.

Real libcbv2g types and EXI codecs; production Config::add and abort helper
execute against minimal slot/vector plumbing with the production API maximum.
Downstream probes execute actual DIN/ISO2 CPD EV-publication blocks.
All identities are synthetic. No threads: TSAN is not applicable to this harness.
"""
from pathlib import Path
import os
import re
import signal
import subprocess
import sys
import tempfile
import unittest
from test_iram_permanent_allocation import function

S = Path(__file__).resolve().parents[2]
SAN = 'sanitize' in sys.argv
if SAN:
    sys.argv.remove('sanitize')


class LateTesla(unittest.TestCase):
    def test_real_decoder_boundaries(self):
        """Generate an over-limit wire vector without indexing a 5-slot object.

        Only the isolated fixture ENCODER sees an enlarged temporary type. The
        decoder uses untouched production headers/source, including its bound.
        No generated library files in the repository are changed.
        """
        lib = S/'.pio/libdeps/warp4_tesla_ble/libcbv2g'
        encode = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cbv2g/app_handshake/appHand_Encoder.h"
int main(int argc,char **argv) {
 assert(argc==2);unsigned n=atoi(argv[1]);assert(n<=6);
 struct appHand_exiDocument doc={0};unsigned char bytes[4096]={0};exi_bitstream_t stream;
 doc.supportedAppProtocolReq_isUsed=1;doc.supportedAppProtocolReq.AppProtocol.arrayLen=n;
 for(unsigned i=0;i<n;++i) {
  struct appHand_AppProtocolType *p=&doc.supportedAppProtocolReq.AppProtocol.array[i];
  strcpy(p->ProtocolNamespace.characters,"urn:iso:15118:2:2013:MsgDef");
  p->ProtocolNamespace.charactersLen=strlen(p->ProtocolNamespace.characters);
  p->VersionNumberMajor=2;p->Priority=i+1;p->SchemaID=i+1;
 }
 exi_bitstream_init(&stream,bytes,sizeof(bytes),0,NULL);
 int ret=encode_appHand_exiDocument(&stream,&doc);assert(ret==0);
 size_t len=exi_bitstream_get_length(&stream);assert(fwrite(bytes,1,len,stdout)==len);
}
'''
        decode = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "cbv2g/app_handshake/appHand_Decoder.h"
#include "cbv2g/common/exi_error_codes.h"
int main(int argc,char **argv) {
 assert(argc==2);unsigned n=atoi(argv[1]);unsigned char bytes[4096]={0};
 size_t len=fread(bytes,1,sizeof(bytes),stdin);assert(len>0);
 struct {unsigned long before;struct appHand_exiDocument doc;unsigned long after;} guard={0};
 guard.before=0xabcdef;guard.after=0xfedcba;exi_bitstream_t stream;
 assert(appHand_AppProtocolType_5_ARRAY_SIZE==5);
 exi_bitstream_init(&stream,bytes,len,0,NULL);
 int ret=decode_appHand_exiDocument(&stream,&guard.doc);
 assert(guard.before==0xabcdef && guard.after==0xfedcba);
 if(n>5) {assert(ret==EXI_ERROR__ARRAY_OUT_OF_BOUNDS);assert(guard.doc.supportedAppProtocolReq.AppProtocol.arrayLen==5);}
 else {assert(ret==0);assert(guard.doc.supportedAppProtocolReq.AppProtocol.arrayLen==n);}
 printf("REAL EXI entries=%u decoder=%d bounded=5 PASS\n",n,ret);
}
'''
        flags = ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie'] if SAN else []
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            d = Path(td)
            header = lib/'include/cbv2g/app_handshake/appHand_Datatypes.h'
            fixture = d/'include/cbv2g/app_handshake/appHand_Datatypes.h'
            fixture.parent.mkdir(parents=True)
            # Encoder header includes Datatypes with a relative include.
            (fixture.parent/'appHand_Encoder.h').write_text((header.parent/'appHand_Encoder.h').read_text())
            text = header.read_text()
            self.assertIn('#define appHand_AppProtocolType_5_ARRAY_SIZE (5)', text)
            fixture.write_text(text.replace('#define appHand_AppProtocolType_5_ARRAY_SIZE (5)', '#define appHand_AppProtocolType_5_ARRAY_SIZE (6)'))
            common = sorted((lib/'lib/cbv2g/common').glob('*.c'))
            for name, code, extra in (('encode6',encode,['-I'+str(d/'include')]),('decode5',decode,[])):
                (d/(name+'.c')).write_text(code)
                source = lib/('lib/cbv2g/app_handshake/appHand_'+('Encoder' if name=='encode6' else 'Decoder')+'.c')
                subprocess.run(['gcc','-std=c99',*flags,*extra,'-I'+str(lib/'include'),str(d/(name+'.c')),str(source),str(lib/'lib/cbv2g/app_handshake/appHand_Datatypes.c'),*map(str,common),'-o',str(d/name)],check=True)
            for n in (1,4,5,6):
                wire = subprocess.run([str(d/'encode6'),str(n)],capture_output=True,check=True).stdout
                subprocess.run([str(d/'decode5'),str(n)],input=wire,check=True)

    def test_production_negotiation_and_session(self):
        common = (S/'src/modules/iso15118/common.cpp').read_text()
        header = (S/'src/modules/iso15118/common.h').read_text()
        policy = (S/'src/modules/iso15118/iso15118.h').read_text()
        config = (S/'src/config.cpp').read_text()
        match = re.search(r'"supported_protocols", Config::Array\(\{\}, &supported_protocols_prototype, 0, ([^,]+),', common)
        assert match, 'production API capacity declaration not found'
        capacity = match.group(1)
        bounds = header[header.index('// Publish every namespace'):header.index('// Convert value')]
        cbv2g = S/'.pio/libdeps/warp4_tesla_ble/libcbv2g'
        vendors = [x.strip().replace(' ', '') for x in (S/'src/modules/iso15118/EVCC Vendor.uint8.enum').read_text().splitlines() if x.strip()]
        methods = '\n'.join(function(policy, x) for x in ('bool is_soc_compatible()', 'bool is_autocharge_only()', 'bool is_autocharge()', 'bool use_nonegotiation_autocharge()'))
        impl = '\n'.join(function(common, x) for x in ('void Common::set_evcc_vendor', 'void Common::detect_evcc_vendor_from_mac', 'void Common::detect_evcc_vendor_from_protocol', 'void Common::handle_supported_app_protocol_req', 'void Common::decode'))
        controller = (S/'src/modules/iso15118/iso15118.cpp').read_text()
        disconnect = controller[controller.index('        if (charger_state->asUint() == 0)'):controller.index('            qca700x.link_down();', controller.index('// Monitor charger state'))]
        publications = []
        for filename in ('din70121.cpp', 'iso2.cpp'):
            text = (S/'src/modules/iso15118'/filename).read_text()
            start = text.index('        ev.set_soc(')
            start = text.rindex('    if (req->DC_EVChargeParameter_isUsed)', 0, start)
            publications.append(text[start:text.index('#endif', start)])
        code = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <utility>
#include "cbv2g/app_handshake/appHand_Datatypes.h"
#include "cbv2g/app_handshake/appHand_Encoder.h"
#include "cbv2g/app_handshake/appHand_Decoder.h"
BOUNDS
[[noreturn]] void esp_system_abort(const char *s) {fprintf(stderr,"PRODUCTION CONFIG ABORT: %s\n",s);abort();}
template<int N,class... A> [[noreturn]] void esp_system_abortf(const char *s,A... args) {fprintf(stderr,"PRODUCTION CONFIG ABORT: ");fprintf(stderr,s,args...);fprintf(stderr,"\n");abort();}
class ArrayConfig;
struct ArraySlot {size_t maxElements=CONFIG_MAX;ArrayConfig *prototype=nullptr;} array_slot;
class ArrayConfig {
public:
 using Wrap=ArrayConfig *;struct ConfArray {};
 struct {struct {struct {ArraySlot *getSlot() const {return &array_slot;}} a;} val;} value;
 std::vector<ArrayConfig> children;
 template<class T> bool is() const {return true;}
 std::vector<ArrayConfig>& asArray() {return children;}
 void set_updated(unsigned) {}
 Wrap add();
};
CONFIG_ABORT
CONFIG_ADD
ArrayConfig array_prototype,array_instance;
#include <sys/socket.h>
#include <arpa/inet.h>
#define OPTIONS_ISO15118_ENABLE_TESTING_OPTIONS() TESTING
#define SESSION_ID_LENGTH 8
#define TF_EVSE_V2_CHARGING_PROTOCOL_ISO15118 1
constexpr int operator""_us(unsigned long long n) { return n; }
enum class EVCCVendor { VENDORS };
enum class CommonState {Idle, ProtocolNegotiated};
struct ISOTLS {};
int api_mutations=0;
struct Node {
 std::map<std::string,Node> children; bool v=false; EVCCVendor vendor=EVCCVendor::Unknown;
 Node *get(const char *s) {return &children[s];}
 const Node *get(const char *s) const {return &children.at(s);}
 bool asBool() const {return v;}
 unsigned asUint() const {return v;}
 template<class T> T asEnum() const {return static_cast<T>(vendor);}
 void updateEnum(EVCCVendor x) {++api_mutations;vendor=x;}
 template<class T> void updateEnum(T) {++api_mutations;}
 std::vector<std::string> strings;
 void updateString(const char *s) {++api_mutations;strings.emplace_back(s);}
 void removeAll() {++api_mutations;array_instance.children.clear();strings.clear();}
 Node *add() {++api_mutations;array_instance.add();return this;}
};
using Config=Node; using ConfigRoot=Node;

const char *get_evcc_vendor_name(EVCCVendor) {return "fixture";}
const char *strnstr(const char *s,const char *needle,size_t n) {
 const size_t len=strlen(needle); for(size_t i=0;i+len<=n;++i) if(!memcmp(s+i,needle,len)) return s+i; return nullptr;
}
template<class... T> void trace_iso(T...) {}
CLASS;
struct EV {int publications=0,sources=0; void set_soc(float) {++publications;} template<class T> void session_updated(T) {++sources;}} ev;
enum class EVDataSource {DIN,ISO2};
enum class EventResult {OK};
struct CPD {bool DC_EVChargeParameter_isUsed=true; struct {struct {int EVRESSSOC=42;} DC_EVStatus;} DC_EVChargeParameter;};
struct ProbeDIN {int calls=0; void handle_bitstream(void *) {++calls; CPD fixture; auto *req=&fixture; PUBDIN }};
struct ProbeISO2 {int calls=0; void handle_bitstream(void *) {++calls; CPD fixture; auto *req=&fixture; PUBISO }};
V2GTP_PAYLOAD_TYPE
struct Probe20 {int calls=0;void handle_bitstream(void *, V2GTPPayloadType) {++calls;}};
struct ISO15118 {
 Common common; Config config,config_experimental; bool nonegotiation_pending=false;
 int communication_setup_deadline=9,reslac_guard_deadline=9,transitions=0,charging_protocol_calls=0;
 bool enabled=true;
 bool is_enabled() const {return enabled;}
 void cancel_pending_tasks() {}
 EventResult disconnect();
 ProbeDIN din70121; ProbeISO2 iso2; Probe20 iso20;
 enum class ModemOff {Delayed};
 void begin_iec_transition(ModemOff) {++transitions;}
 void set_charging_protocol(int,int) {++charging_protocol_calls;}
 template<class... T> void trace(T...) {}
 METHODS
} iso15118;
V2GTP_HEADER
static_assert(sizeof(V2GTP_Header)==8);
int bitstream_setups=0, allocations=0;
void tracked_bitstream_init(exi_bitstream *s,uint8_t *data,size_t size,size_t offset,exi_status_callback cb) {
 ++bitstream_setups;exi_bitstream_init(s,data,size,offset,cb);
}
#define exi_bitstream_init tracked_bitstream_init

void *calloc_psram_or_dram(size_t n,size_t s) {++allocations;return calloc(n,s);}
void Common::send_exi(ExiType) {}
void Common::reset_active_socket() {} // OS boundary; vendor reset remains production code.
EventResult ISO15118::disconnect() {
 Node disconnected; const Node *charger_state=&disconnected;
 DISCONNECT
 } return EventResult::OK;
}
TABLE
IMPL
void offer(Common &c,const char *ns) {
 auto &a=c.appHandDec->supportedAppProtocolReq.AppProtocol;
 assert(a.arrayLen<appHand_AppProtocolType_5_ARRAY_SIZE);
 auto &p=a.array[a.arrayLen++]; strcpy(p.ProtocolNamespace.characters,ns);
 p.ProtocolNamespace.charactersLen=strlen(ns); p.SchemaID=a.arrayLen;
 p.VersionNumberMajor=2;p.VersionNumberMinor=0;p.Priority=a.arrayLen;
}
void roundtrip(appHand_exiDocument &doc) {
 uint8_t bytes[4096]={};exi_bitstream stream;
 doc.supportedAppProtocolReq_isUsed=1;
 const auto original=doc;
 exi_bitstream_init(&stream,bytes,sizeof(bytes),0,nullptr);
 assert(encode_appHand_exiDocument(&stream,&doc)==0);
 const size_t n=exi_bitstream_get_length(&stream);
 exi_bitstream_init(&stream,bytes,n,0,nullptr);
 doc={};assert(decode_appHand_exiDocument(&stream,&doc)==0);
 assert(doc.supportedAppProtocolReq_isUsed);
 const auto &before=original.supportedAppProtocolReq.AppProtocol;
 const auto &after=doc.supportedAppProtocolReq.AppProtocol;
 assert(after.arrayLen==before.arrayLen);
 for(unsigned i=0;i<after.arrayLen;++i) {
  assert(after.array[i].SchemaID==before.array[i].SchemaID);
  assert(after.array[i].ProtocolNamespace.charactersLen==before.array[i].ProtocolNamespace.charactersLen);
  assert(!strcmp(after.array[i].ProtocolNamespace.characters,before.array[i].ProtocolNamespace.characters));
 }
}
int main(int argc,char **argv) {
 assert(argc==2); std::string mode=argv[1]; auto &p=iso15118; auto &c=p.common;
 array_slot.prototype=&array_prototype;
 if(mode=="capacity-overflow") {
  for(size_t i=0;i<array_slot.maxElements;++i) array_instance.add();
  assert(array_instance.children.size()==SUPPORTED_APP_PROTOCOL_CAPACITY);
  array_instance.add();return 99; // Must abort, not silently truncate or grow.
 }
 c.api_state.get("evcc_vendor");
 if(mode=="empty-boundary") {
  // Exact-size storage: no padding hides the pinned codec's initial read.
  // Every route must reject before setup, allocation or API mutation.
  for(auto route:{Common::ExiType::AppHand,Common::ExiType::Din,Common::ExiType::Iso2,Common::ExiType::Iso20}) {
   for(uint16_t type:{uint16_t(0x8001),uint16_t(0x8002),uint16_t(0x8003)}) {
    for(size_t length=0;length<=sizeof(V2GTP_Header);++length) {
     for(uint32_t declared:{0u,1u,0xffffffffu}) {
      V2GTP_Header frame{1,0xfe,htons(type),htonl(declared)};
      appHand_exiDocument dec{},enc{};dec.supportedAppProtocolReq_isUsed=1;enc.supportedAppProtocolRes_isUsed=1;
      c.appHandDec=&dec;c.appHandEnc=&enc;c.exi_in_use=route;c.state=CommonState::ProtocolNegotiated;
      c.api_state.get("protocol")->strings={"unchanged"};
      c.api_state.get("supported_protocols")->strings={"sentinel"};
      c.set_evcc_vendor(EVCCVendor::Tesla);
      const auto before_dec=dec,before_enc=enc;
      const int setups=bitstream_setups,allocs=allocations,mutations=api_mutations;
      c.decode(reinterpret_cast<uint8_t*>(&frame),length);
      assert(bitstream_setups==setups && allocations==allocs);
      assert(api_mutations==mutations);
      assert(!memcmp(&dec,&before_dec,sizeof(dec)) && !memcmp(&enc,&before_enc,sizeof(enc)));
      assert(c.exi_in_use==route && c.state==CommonState::ProtocolNegotiated);
      assert(c.get_evcc_vendor()==EVCCVendor::Tesla);
      assert(c.api_state.get("protocol")->strings==std::vector<std::string>{"unchanged"});
      assert(c.api_state.get("supported_protocols")->strings==std::vector<std::string>{"sentinel"});
      assert(p.communication_setup_deadline==9 && p.reslac_guard_deadline==9 && !p.nonegotiation_pending);
      assert(!p.transitions && !p.charging_protocol_calls && !p.din70121.calls && !p.iso2.calls && !p.iso20.calls);
      assert(!ev.publications && !ev.sources);
     }
    }
   }
  }
  c.appHandDec=nullptr;c.appHandEnc=nullptr;c.exi_in_use=Common::ExiType::AppHand;
  V2GTP_Header empty{1,0xfe,htons(0x8001),0};
  const int allocs=allocations,setups_before=bitstream_setups;
  c.decode(reinterpret_cast<uint8_t*>(&empty),sizeof(empty));
  assert(!c.appHandDec && !c.appHandEnc && allocations==allocs && bitstream_setups==setups_before);
  // Smallest real non-empty appHand response is accepted, not blanket-rejected.
  appHand_exiDocument response{},dec{},enc{};response.supportedAppProtocolRes_isUsed=1;
  response.supportedAppProtocolRes.ResponseCode=appHand_responseCodeType_Failed_NoNegotiation;
  uint8_t bytes[64]{};exi_bitstream stream;
  exi_bitstream_init(&stream,bytes,sizeof(bytes),sizeof(V2GTP_Header),nullptr);
  assert(encode_appHand_exiDocument(&stream,&response)==0);
  const size_t size=exi_bitstream_get_length(&stream);
  V2GTP_Header h{1,0xfe,htons(0x8001),htonl(size)};memcpy(bytes,&h,sizeof(h));
  c.exi_in_use=Common::ExiType::AppHand;c.appHandDec=&dec;c.appHandEnc=&enc;
  const int setups=bitstream_setups;c.decode(bytes,sizeof(h)+size);
  assert(bitstream_setups==setups+1 && dec.supportedAppProtocolRes_isUsed);
  printf("COMMON_BOUNDARY testing=%d: 324 rejected frames + smallest response (%zu bytes) PASS\n",TESTING,size);
  return 0;
 }
 uint8_t unknown[]={2,2,2,1,2,3}, other[]={2,2,2,1,2,4}, bmw[]={0,1,0xa9,1,2,3};
 for(unsigned bits=0;bits<64;++bits) {
  p.config.get("autocharge")->v=bits&1; p.config.get("read_soc")->v=bits&2;
  p.config.get("charge_via_iso15118")->v=bits&4;
  p.config_experimental.get("ignore_soc_compatibility")->v=bits&8;
  p.config_experimental.get("nonegotiation_autocharge")->v=bits&16;
  p.nonegotiation_pending=bits&32;
  c.reset_evcc_vendor(); c.detect_evcc_vendor_from_mac(unknown);
  assert(c.get_evcc_vendor()==EVCCVendor::Unknown);
  if(mode=="same-mac" || mode=="same-bmw" || mode=="changed-mac" || mode=="changed-bmw" || mode=="disconnect") {
   if(mode=="same-bmw") c.detect_evcc_vendor_from_mac(bmw);
   const char *ns="urn:tesla:din:2018:MsgDef"; c.detect_evcc_vendor_from_protocol(ns,strlen(ns));
   if(mode=="disconnect") {p.enabled=bits&1; p.disconnect();}
   c.detect_evcc_vendor_from_mac(mode=="changed-bmw" || mode=="same-bmw"?bmw:mode=="changed-mac"?other:unknown);
   const auto expected=mode=="same-mac" || mode=="same-bmw"?EVCCVendor::Tesla:mode=="changed-bmw"?EVCCVendor::BMW:EVCCVendor::Unknown;
   if(c.get_evcc_vendor()!=expected) {fprintf(stderr,"FAIL %s bits=%u: vendor=%u expected=%u after retry/reset\n",argv[1],bits,unsigned(c.get_evcc_vendor()),unsigned(expected)); return 1;}
   continue;
  }
  bool five=mode.rfind("five-",0)==0;
  bool four=mode=="four-standard";
  bool tesla=mode=="mixed-din" || mode=="mixed-iso2" || mode=="tesla-only" || mode=="reverse" || mode.rfind("five-tesla-",0)==0;
  if(mode=="bmw") c.detect_evcc_vendor_from_mac(bmw);
  appHand_exiDocument dec{},enc{}; c.appHandDec=&dec; c.appHandEnc=&enc;
  c.state=CommonState::Idle; c.exi_in_use=Common::ExiType::AppHand;
  p.transitions=0; p.charging_protocol_calls=0; ev={}; p.din70121.calls=0; p.iso2.calls=0;p.iso20.calls=0;
  if(five || four) {
   const char *mixed[]={"urn:din:70121:2012:MsgDef","urn:iso:15118:2:2013:MsgDef","urn:iso:15118:-20:AC","urn:fixture:unknown","urn:iso:15118:2:2013:MsgDef"};
   unsigned standard_index=0;
   for(unsigned i=0;i<appHand_AppProtocolType_5_ARRAY_SIZE-unsigned(four);++i) {
    const bool tesla_slot=tesla && i==unsigned(mode.back()-'0');
    const char *ns=tesla ? (tesla_slot?"urn:tesla:din:2018:MsgDef":mixed[standard_index++]) : mixed[i];
    if(mode=="five-din") ns=i==4?mixed[0]:mixed[3];
    if(mode=="five-iso20") ns=i==4?mixed[2]:mixed[3];
    if(mode=="five-unknown") ns=mixed[3];
    offer(c,ns);
   }
  } else {
  if(tesla && mode!="reverse") offer(c,"urn:tesla:din:2018:MsgDef");
  if(mode=="unknown-only") offer(c,"urn:fixture:unknown");
  else if(mode!="tesla-only") offer(c,mode=="mixed-din"?"urn:din:70121:2012:MsgDef":"urn:iso:15118:2:2013:MsgDef");
  if(mode=="reverse") offer(c,"urn:tesla:din:2018:MsgDef");
  }
  roundtrip(dec);
  const bool forced=p.nonegotiation_pending || (p.is_autocharge_only() && p.use_nonegotiation_autocharge());
  // Exercise complete requests through the real Common entry and wire header.
  uint8_t request[4096]{};exi_bitstream request_stream;
  exi_bitstream_init(&request_stream,request,sizeof(request),sizeof(V2GTP_Header),nullptr);
  assert(encode_appHand_exiDocument(&request_stream,&dec)==0);
  const size_t request_size=exi_bitstream_get_length(&request_stream);
  V2GTP_Header request_header{1,0xfe,htons(0x8001),htonl(request_size)};
  memcpy(request,&request_header,sizeof(request_header));
  c.decode(request,sizeof(request_header)+request_size);
  assert(array_instance.children.size()==dec.supportedAppProtocolReq.AppProtocol.arrayLen);
  const auto &published=c.api_state.get("supported_protocols")->strings;
  assert(published.size()==dec.supportedAppProtocolReq.AppProtocol.arrayLen);
  for(size_t i=0;i<published.size();++i) assert(published[i]==dec.supportedAppProtocolReq.AppProtocol.array[i].ProtocolNamespace.characters);
  const int response=enc.supportedAppProtocolRes.ResponseCode;
  const bool reject=tesla || mode=="unknown-only" || mode=="five-unknown" || forced;
  assert(enc.supportedAppProtocolRes_isUsed==1);
  assert(enc.supportedAppProtocolRes.SchemaID_isUsed==(reject?0:1));
  if(reject) assert(c.exi_in_use==Common::ExiType::AppHand);
  // Feed a complete EXI response through the actual production dispatcher.
  // A complete encoded response preserves the negotiated downstream probe.
  struct {V2GTP_Header header;uint8_t payload[512];} frame{};
  frame.header={1,0xfe,htons(0x8001),0};
  appHand_exiDocument followup{};followup.supportedAppProtocolRes_isUsed=1;
  followup.supportedAppProtocolRes.ResponseCode=appHand_responseCodeType_Failed_NoNegotiation;
  exi_bitstream stream;exi_bitstream_init(&stream,frame.payload,sizeof(frame.payload),0,nullptr);
  assert(encode_appHand_exiDocument(&stream,&followup)==0);
  frame.header.payload_length=htonl(exi_bitstream_get_length(&stream));
  c.decode(reinterpret_cast<uint8_t*>(&frame),sizeof(frame.header)+exi_bitstream_get_length(&stream));

  if(tesla && (ev.publications || p.din70121.calls || p.iso2.calls || p.iso20.calls)) {
   fprintf(stderr,"FAIL %s bits=%u: late Tesla reached PLC: DIN=%d ISO2=%d SoC=%d\n",argv[1],bits,p.din70121.calls,p.iso2.calls,ev.publications); return 1;
  }
  assert(response==(reject?appHand_responseCodeType_Failed_NoNegotiation:appHand_responseCodeType_OK_SuccessfulNegotiation));
  if((five || four) && !reject) {
   const auto expected=mode=="five-din"?Common::ExiType::Din:mode=="five-iso20"?Common::ExiType::Iso20:Common::ExiType::Iso2;
   assert(c.exi_in_use==expected);assert(enc.supportedAppProtocolRes.SchemaID==(four?2:5));
  }
  assert(c.state==(reject?CommonState::Idle:CommonState::ProtocolNegotiated));
  assert(p.transitions==(reject?1:0)); assert(p.charging_protocol_calls==(reject?0:1));
  const bool publish=!reject && mode!="five-iso20";
  assert(ev.publications==(publish?1:0)); assert(ev.sources==(publish?1:0));
  assert(p.iso20.calls==(!reject && mode=="five-iso20"?1:0));
  assert(p.nonegotiation_pending==false || !reject);
  if(tesla) assert(c.get_evcc_vendor()==EVCCVendor::Tesla);
 }
 printf("LATE_TESLA testing=%d %s: 64 combinations PASS\n",TESTING,argv[1]);
}
'''
        replacements = {
            'V2GTP_PAYLOAD_TYPE': function((S/'src/modules/iso15118/sdp.h').read_text(), 'enum class V2GTPPayloadType')+';',
            'V2GTP_HEADER': function((S/'src/modules/iso15118/sdp.h').read_text(), 'struct [[gnu::packed]] V2GTP_Header')+';',
            'BOUNDS': bounds, 'CONFIG_MAX': capacity,
            'CONFIG_ABORT': function(config, 'static void abort_on_array_add_max_failure'),
            'CONFIG_ADD': function(config, 'Config::Wrap Config::add()').replace('Config', 'ArrayConfig'),
            'VENDORS': ','.join(vendors),
            'CLASS': function(header, 'class Common final').replace('private:', 'public:').strip(),
            'METHODS': methods, 'IMPL': impl,
            'DISCONNECT': disconnect,
            'TABLE': common[common.index('struct EVCCVendorOUI'):common.index('void Common::set_evcc_vendor')],
            'PUBDIN': publications[0], 'PUBISO': publications[1],
        }
        for key, value in replacements.items():
            code = code.replace(key, value)
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as td:
            d = Path(td)
            (d/'test.cpp').write_text(code)
            flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-no-pie'] if SAN else []
            # Instrument real C codec sources too, not an opaque prebuilt archive.
            objects = []
            sources = sorted((cbv2g/'lib/cbv2g/common').glob('*.c')) + sorted((cbv2g/'lib/cbv2g/app_handshake').glob('*.c'))
            assert sources, 'libcbv2g sources required; no mock codec fallback'
            for source in sources:
                obj = d/(source.stem+'.o')
                subprocess.run(['gcc', '-std=c99', '-I'+str(cbv2g/'include'), *flags, '-c', str(source), '-o', str(obj)], check=True)
                objects.append(str(obj))
            for testing in (0, 1):
                subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(cbv2g/'include'), f'-DTESTING={testing}', *flags, str(d/'test.cpp'), *objects, '-o', str(d/'test')], check=True)
                result = subprocess.run([str(d/'test'),'capacity-overflow'], capture_output=True, text=True)
                self.assertEqual(result.returncode, -signal.SIGABRT)
                self.assertIn('max allowed number of elements (5)', result.stderr)
                print(f'CONFIG testing={testing}: max+1 rejected by production abort PASS', flush=True)
                for mode in ('empty-boundary','five-tesla-0','five-tesla-1','five-tesla-2','five-tesla-3','five-tesla-4','five-standard','five-din','five-iso20','five-unknown','four-standard','mixed-din','mixed-iso2','reverse','tesla-only','unknown-only','unknown-standard','bmw','same-mac','same-bmw','changed-mac','changed-bmw','disconnect'):
                    with self.subTest(testing=testing, mode=mode):
                        subprocess.run([str(d/'test'),mode], check=True)


if __name__ == '__main__':
    unittest.main()
