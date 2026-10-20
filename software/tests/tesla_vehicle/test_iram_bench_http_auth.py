#!/usr/bin/env python3
"""Actual middleware registration/strict boundary + users callback + digest code.
Only HTTP/Arduino/scheduler/config adapters are synthetic; no auth substitute.
"""
from pathlib import Path
import os, re, subprocess, tempfile, unittest
from test_iram_bench_pairing import ROOT, MBED, LIB, SANITIZED_LIB

def function(text, signature):
    start=text.index(signature); opening=text.index('{',start); level=1; end=opening+1
    while level:
        if text[end]=='{': level+=1
        if text[end]=='}': level-=1
        end+=1
    return text[start:end]

SHIM=r'''
#include <string>
#include <vector>
#include <functional>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <mbedtls/md5.h>
// Fail selected hash-output allocations only, after the parser and earlier
// hashes have succeeded. Never substitute an auth result.
int hash_allocations=0, fail_hash_allocation=0, hash_calls=0, fail_hash_call=0;
bool fail_hash(){return ++hash_calls==fail_hash_call;}
int fault_md5(const unsigned char *p,size_t n,unsigned char *out){return fail_hash()?-1:mbedtls_md5(p,n,out);}
int fault_md5_starts(mbedtls_md5_context *c){return fail_hash()?-1:mbedtls_md5_starts(c);}
int fault_md5_update(mbedtls_md5_context *c,const unsigned char *p,size_t n){return fail_hash()?-1:mbedtls_md5_update(c,p,n);}
int fault_md5_finish(mbedtls_md5_context *c,unsigned char *p){return fail_hash()?-1:mbedtls_md5_finish(c,p);}
#define mbedtls_md5 fault_md5
#define mbedtls_md5_starts fault_md5_starts
#define mbedtls_md5_update fault_md5_update
#define mbedtls_md5_finish fault_md5_finish
class String:public std::string {
public:
 using std::string::string;String(const std::string &s):std::string(s){}
 bool isEmpty()const{return empty();} bool equals(const String &s)const{return *this==s;}
 bool startsWith(const char *s)const{return rfind(s,0)==0;}
 int indexOf(const char *s)const{auto p=find(s);return p==npos?-1:int(p);}int indexOf(const char c)const{auto p=find(c);return p==npos?-1:int(p);}
 String substring(size_t a)const{return substr(a);} String substring(size_t a,size_t b)const{return substr(a,b-a);}
 void trim(){auto a=find_first_not_of(" \t\r\n");auto b=find_last_not_of(" \t\r\n");assign(a==npos?"":substr(a,b-a+1));}
 void concat(const char *s){append(s);}void concat(const String &s){append(s);}
 const char *asEphemeralCStr()const{return c_str();}
};
class CoolString {
 std::vector<char> b;
public:bool reserve(size_t n){if(++hash_allocations==fail_hash_allocation)return false;b.resize(n);return true;}char *begin(){return b.data();}void setLength(size_t n){b[n]=0;}operator String()const{return b.data();}
};
struct Log{template<class... T>void printfln(const char *,T...) {}} logger;
uint32_t now_us_value=1;struct Time{template<class T>T as(){return T(now_us_value);}};Time now_us(){return {};}
uint32_t build_timestamp(){return 1;}uint32_t esp_random(){return 1;}
#define DEFAULT_REALM "esp32-lib"
struct AuthFields {String username,realm,nonce,uri,response,qop,nc,cnonce,opaque;bool success;};
static bool http_thread=true;
struct Request {String authorization;String uri_value="/tesla_vehicle/bench_pair_start";String method="PUT";String header(const char *)const{return authorization;}const char *methodString()const{return method.c_str();}const char *uri()const{return uri_value.c_str();}bool is_strictly_authenticated()const{return strict_authenticated;}private:friend class WebServer;bool strict_authenticated=false;};
using WebServerRequest=Request;
struct Config {String value;std::vector<Config> entries;Config *get(size_t i){return &entries.at(i);}Config *get(const char *name){return this+(!strcmp(name,"digest_hash")?1:0);}size_t count(){return entries.size();}String asString(){return value;}const char *asEphemeralCStr(){return value.c_str();}};
struct {Config config;} users;
struct {template<class F>bool await(F f){assert(http_thread);http_thread=false;f();http_thread=true;return true;}} task_scheduler;
class WebServer {
 std::function<bool(WebServerRequest)> auth_fn,strict_auth_fn;
public:
 void onAuthenticate_HTTPThread(std::function<bool(WebServerRequest)> &&,std::function<bool(WebServerRequest)> && = nullptr);
 bool authenticate_strict_HTTPThread(WebServerRequest);
 void mark_authenticated_HTTPThread(WebServerRequest &);
 bool ordinary(WebServerRequest r){return !auth_fn||auth_fn(r);}
};
'''
MAIN=r'''
int main(){
 WebServer server;WebServerRequest request;
 // Disabled effective authentication: ordinary middleware allows; strict refuses.
 server.onAuthenticate_HTTPThread(nullptr);assert(server.ordinary(request));assert(!server.authenticate_strict_HTTPThread(request));assert(!boundary(server,request,true).is_strictly_authenticated());
 // Real users callback and real RFC digest verification; temporary test user only.
 users.config.entries.resize(2);users.config.entries[0].value="bench-test";users.config.entries[1].value=generateDigestHash("bench-test","temporary-test-only",DEFAULT_REALM);
 // Config adapter: users.config.get("users") returns same entries; count=2 is
 // represented as one user with contiguous username/hash slots.
 server.onAuthenticate_HTTPThread([](WebServerRequest r){return check_http_auth(r);},[](WebServerRequest r){return check_http_auth(r,false);});
 assert(!server.ordinary(request)&&!server.authenticate_strict_HTTPThread(request));assert(!boundary(server,request,true).is_strictly_authenticated());
 request.authorization="Basic invalid";assert(!server.authenticate_strict_HTTPThread(request));
 request.authorization="Digest username=\"bench-test\", realm=\"esp32-lib\", nonce=\"test-nonce\", uri=\"/tesla_vehicle/bench_pair_start\", response=\"invalid\", qop=auth, nc=00000001, cnonce=\"test-cnonce\"";
 assert(!server.authenticate_strict_HTTPThread(request));assert(!boundary(server,request,true).is_strictly_authenticated());
 String ha2=stringMD5({"PUT:/tesla_vehicle/bench_pair_start"});String response=stringMD5({users.config.entries[1].value.c_str(),":test-nonce:00000001:test-cnonce:auth:",ha2.c_str()});
 request.authorization="Digest username=\"bench-test\", realm=\"esp32-lib\", nonce=\"test-nonce\", uri=\"/tesla_vehicle/bench_pair_start\", response=\""+response+"\", qop=auth, nc=00000001, cnonce=\"test-cnonce\"";
 assert(server.ordinary(request)&&server.authenticate_strict_HTTPThread(request));
 Request dispatched=boundary(server,request,true);assert(!boundary(server,request,false).is_strictly_authenticated());http_thread=false;assert(dispatched.is_strictly_authenticated());http_thread=true;
 request.uri_value="/tesla_vehicle/bench_pair_delete";assert(!server.authenticate_strict_HTTPThread(request));server.mark_authenticated_HTTPThread(request);assert(!request.is_strictly_authenticated());request.uri_value="/tesla_vehicle/bench_pair_start";
 // Reproduce the dangerous final-output allocation after HA2 succeeds. Empty
 // supplied response must NEVER compare equal to an empty computed digest.
 const String valid_header=request.authorization;
 const auto response_start=valid_header.find("response=\"")+10;
 auto with_response=[&](const String &r){return String(valid_header.substr(0,response_start)+r+valid_header.substr(response_start+32));};
 request.authorization=with_response("");hash_allocations=0;fail_hash_allocation=2;
 assert(!boundary(server,request,true).is_strictly_authenticated());
 fail_hash_allocation=0;
 for(const String &r:std::vector<String>{"",String(31,'a'),String(33,'a'),String(32,'z')}){
  request.authorization=with_response(r);assert(!parseDigestAuth(request.authorization.substr(7).c_str()).success);
  assert(!boundary(server,request,true).is_strictly_authenticated());
 }
 // Valid credentials remain valid after every selectively injected failure.
 request.authorization=valid_header;hash_allocations=0;hash_calls=0;
 assert(boundary(server,request,true).is_strictly_authenticated());
 const int allocation_count=hash_allocations, call_count=hash_calls;
 assert(allocation_count>=2&&call_count>=2);
 for(int i=1;i<=allocation_count;++i){hash_allocations=0;fail_hash_allocation=i;assert(!boundary(server,request,true).is_strictly_authenticated());}
 fail_hash_allocation=0;
 for(int i=1;i<=call_count;++i){hash_calls=0;fail_hash_call=i;assert(!boundary(server,request,true).is_strictly_authenticated());}
 fail_hash_call=0;assert(boundary(server,request,true).is_strictly_authenticated());
 // The ordinary password (non-HA1) path must fail closed too, at each of its
 // three hashes. Authentication still works when no allocation/hash fails.
 const auto fields=parseDigestAuth(valid_header.substr(7).c_str());
 hash_allocations=0;hash_calls=0;
 assert(checkDigestAuthentication(fields,"PUT","bench-test","temporary-test-only",DEFAULT_REALM,false,nullptr,nullptr,request.uri()));
 const int raw_allocations=hash_allocations,raw_calls=hash_calls;assert(raw_allocations==3);
 for(int i=1;i<=raw_allocations;++i){hash_allocations=0;fail_hash_allocation=i;assert(!checkDigestAuthentication(fields,"PUT","bench-test","temporary-test-only",DEFAULT_REALM,false,nullptr,nullptr,request.uri()));}
 fail_hash_allocation=0;
 for(int i=1;i<=raw_calls;++i){hash_calls=0;fail_hash_call=i;assert(!checkDigestAuthentication(fields,"PUT","bench-test","temporary-test-only",DEFAULT_REALM,false,nullptr,nullptr,request.uri()));}
 fail_hash_call=0;assert(checkDigestAuthentication(fields,"PUT","bench-test","temporary-test-only",DEFAULT_REALM,false,nullptr,nullptr,request.uri()));
 auto missing=valid_header;missing.erase(missing.find("response=\""),strlen("response=\"")+32+3);request.authorization=missing;
 assert(!boundary(server,request,true).is_strictly_authenticated());request.authorization=valid_header;
 for(const String &bad_hash:std::vector<String>{"",String(31,'a'),String(33,'a'),String(32,'z')}){assert(!checkDigestAuthentication(fields,"PUT","bench-test",bad_hash.c_str(),DEFAULT_REALM,true,nullptr,nullptr,request.uri()));}
 printf("strict middleware: %d selective hash-output allocations and %d hash error sites refused; raw-password: %d allocations and %d hash error sites refused; valid auth restored\n",allocation_count,call_count,raw_allocations,raw_calls);
 // Runtime last-password deletion causes legacy lockout recovery, NEVER strict.
 users.config.entries[1].value="";assert(server.ordinary(request));assert(!server.authenticate_strict_HTTPThread(request));assert(!boundary(server,request,true).is_strictly_authenticated());
 server.onAuthenticate_HTTPThread(nullptr);assert(!server.authenticate_strict_HTTPThread(request));
}
'''
class BenchHttpAuthTests(unittest.TestCase):
    def test_real_middleware_digest_and_lockout_boundary(self):
        web=(ROOT/'software/src/modules/web_server/web_server.cpp').read_text()
        user=(ROOT/'software/src/modules/users/users.cpp').read_text()
        auth=(ROOT/'software/src/digest_auth.cpp').read_text()
        h=(ROOT/'software/src/modules/web_server/web_server.h').read_text()
        self.assertIn('authenticate_strict_HTTPThread',h)
        self.assertIn('bool allow_lockout',user)
        parts=[function(web,'void WebServer::onAuthenticate_HTTPThread'),function(web,'bool WebServer::authenticate_strict_HTTPThread'),function(web,'void WebServer::mark_authenticated_HTTPThread'),function(user,'static bool check_http_auth')]
        # Execute the actual low_level_handler pre-dispatch boundary block,
        # not a synthetic auth boolean. Only httpd/port/request adapters differ.
        block='constexpr char bench_prefix[] ='+web.split('constexpr char bench_prefix[] =',1)[1].split('    const char *uri = req->uri;',1)[0]
        block='\n'.join(line for line in block.splitlines() if not line.lstrip().startswith('#'))
        parts.append('Request boundary(WebServer &web,Request input,bool supports) {assert(http_thread);Request request;request.authorization=input.authorization;request.uri_value=input.uri_value;request.method=input.method;struct {const char *uri;} lowreq{request.uri()};auto *req=&lowreq;struct {bool supports_user_authentication;} ports{supports};auto *port_handlers=&ports;auto *server=&web;'+block+'\nreturn request;}')
        auth='\n'.join(line for line in auth.splitlines() if not line.startswith('#include'))
        # Adapter distinguishes array from username/hash fields.
        shim=SHIM.replace('size_t count(){return entries.size();}','size_t count(){return entries.empty()?0:1;}').replace('Config *get(const char *name){return this+(!strcmp(name,"digest_hash")?1:0);}', 'Config *get(const char *name){return !strcmp(name,"users")?this:(this+(!strcmp(name,"digest_hash")?1:0));}')
        with tempfile.TemporaryDirectory() as d:
            t=Path(d);(t/'test.cpp').write_text(shim+'\n'+auth+'\n'+'\n'.join(parts)+'\n'+MAIN)
            lib=LIB if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')!='1' else SANITIZED_LIB
            cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(MBED/'include'),str(t/'test.cpp'),str(lib/'library/libmbedcrypto.a'),str(lib/'3rdparty/everest/libeverest.a'),str(lib/'3rdparty/p256-m/libp256m.a'),'-o',str(t/'test')]
            if os.environ.get('TESLA_BLE_SMOKE_SANITIZE')=='1':cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g','-no-pie']
            p=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([str(t/'test')],capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr)
            print(p.stdout,end='')
    def test_thread_context_evidence_before_main_thread_dispatch(self):
        web=(ROOT/'software/src/modules/web_server/web_server.cpp').read_text()
        api=(ROOT/'software/src/modules/tesla_vehicle/tesla_ble_bench_discovery_api.cpp').read_text()
        self.assertIn('server->mark_authenticated_HTTPThread(request)',web)
        self.assertLess(web.index('server->mark_authenticated_HTTPThread(request)'),web.index('handler->callback(*rq.req)'))
        self.assertNotIn('server.authenticate_strict_HTTPThread(request)',api)
        self.assertEqual(api.count('request.is_strictly_authenticated()'),9)
    def test_all_discovery_endpoints_require_strict_auth_before_admission(self):
        src=(ROOT/'software/src/modules/tesla_vehicle/tesla_ble_bench_discovery_api.cpp').read_text()
        blocks=src.split('server.on(')[1:];self.assertEqual(len(blocks),9)
        for block in blocks:
            self.assertIn('request.is_strictly_authenticated()',block)
            self.assertLess(block.index('request.is_strictly_authenticated()'),block.index('local_ethernet_request(request)'))
if __name__=='__main__':unittest.main()
