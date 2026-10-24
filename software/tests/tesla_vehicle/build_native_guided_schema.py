#!/usr/bin/env python3
"""Compile the authored Config::Object and actual from_json overloads.
Only storage/String adapters replace embedded allocation; schema and deserializer
bodies are verbatim production code. No regex key/type/schema reconstruction.
"""
from pathlib import Path
import hashlib,json,os,subprocess
S=Path(__file__).resolve().parents[2]
cpp=(S/'src/modules/tesla_vehicle/tesla_vehicle.cpp').read_text()
inc=(S/'src/modules/tesla_vehicle/tesla_profiles.inc').read_text()
vis=(S/'src/config/visitors.h').read_text()
def block(text,marker):
    start=text.index(marker);at=text.index('{',start);depth=1;i=at+1
    while depth:
        if text[i]=='{':depth+=1
        elif text[i]=='}':depth-=1
        i+=1
    return text[start:i]
profile=block(inc,'Config TeslaVehicle::make_profile()').replace('Config TeslaVehicle::make_profile()','Config make_profile()')
start=cpp.index('Config::Object({',cpp.index('tesla->guided_command ='))
end=cpp.index('});',start)+2
schema=cpp[start:end]
methods='\n'.join(block(vis,'UpdateResult operator()(Config::'+kind+' &x)') for kind in ('ConfString','ConfUint','ConfInt','ConfBool','ConfObject'))
ev=next(x for x in (S/'src/modules/ev/ev.h').read_text().splitlines() if x.startswith('#define EV_MAX_EVS '))
base=r'''
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <cstdint>
#include <climits>
#include <ArduinoJson.h>
struct String:std::string {using std::string::string;String(const std::string&s):std::string(s){} bool isEmpty()const{return empty();}};
using CoolString=String;
namespace ArduinoJson {template<> struct Converter<String> {
 static String fromJson(JsonVariantConst v){return String(v.as<std::string>());}
 static bool checkJson(JsonVariantConst v){return v.is<const char*>();}
};}
struct UpdateResult {String message;bool changed;};
struct Config {
 struct StrSlot {String val;uint32_t minChars,maxChars;};
 struct UintSlot {uint32_t val,min,max;};
 struct IntSlot {int32_t val,min,max;};
 struct BoolSlot {bool val;};
 struct ObjSlot;
 template<class T> struct Wrap {std::shared_ptr<T>s;T*getSlot(){return s.get();}};
 struct ConfString:Wrap<StrSlot>{String*getVal(){return &s->val;}};
 struct ConfUint:Wrap<UintSlot>{uint32_t*getVal(){return &s->val;}};
 struct ConfInt:Wrap<IntSlot>{int32_t*getVal(){return &s->val;}};
 struct ConfBool{bool value;};
 struct ConfObject:Wrap<ObjSlot>{};
 using Value=std::variant<ConfString,ConfUint,ConfInt,ConfBool,ConfObject>;
 Value value;
 static Config Str(const char*v,uint32_t min,uint32_t max){return {ConfString{{std::make_shared<StrSlot>(StrSlot{v,min,max})}}};}
 static Config Uint(uint32_t v,uint32_t min,uint32_t max){return {ConfUint{{std::make_shared<UintSlot>(UintSlot{v,min,max})}}};}
 static Config Int(int32_t v,int32_t min,int32_t max){return {ConfInt{{std::make_shared<IntSlot>(IntSlot{v,min,max})}}};}
 static Config Bool(bool v){return {ConfBool{v}};}
 static Config Object(std::initializer_list<std::pair<const char*,Config>> v);
 void set_updated(uint8_t){}
 static const char*ConfirmKey(){return "confirm";}
 template<class Visitor> static UpdateResult apply_visitor(Visitor v,Value &value){return std::visit(v,value);}
};
struct Config::ObjSlot {struct Key{const char*p;const char*get_val()const{return p;}};struct Schema {size_t length;std::vector<Key> keys;};std::shared_ptr<Schema> owner;Schema *schema;mutable std::vector<Config> values;};
Config Config::Object(std::initializer_list<std::pair<const char*,Config>> v){auto slot=std::make_shared<ObjSlot>();slot->owner=std::make_shared<ObjSlot::Schema>();slot->schema=slot->owner.get();slot->schema->length=v.size();for(auto &p:v){slot->schema->keys.push_back({p.first});slot->values.push_back(p.second);}return {ConfObject{{slot}}};}
struct from_json {
 JsonVariant json_node;
 bool force_same_keys=true;
 bool permit_null_updates=false;
 bool is_root=true;
'''
code=base+methods+'\n};\n'+ev+'\n'+profile+'\nint main(){auto schema='+schema+r''';std::string input;std::getline(std::cin,input);DynamicJsonDocument doc(8192);auto error=deserializeJson(doc,input);if(error){std::cout<<error.c_str();return 2;}auto result=Config::apply_visitor(from_json{doc.as<JsonVariant>(),true,false,true},schema.value);if(!result.message.isEmpty()){std::cout<<result.message;return 1;}std::cout<<"NATIVE_CONFIG_GUIDED_PASS";return 0;}
'''
deps=Path(os.environ.get('TESLA_HOST_ARDUINOJSON_SOURCE',str(S/'.pio/libdeps/warp4_tesla_ble_nimble_review/ArduinoJson')))
out=Path(os.environ.get('TESLA_VFS_TEST_OUT',str(S/'.pio/tesla-host-secure')))/'native-guided-schema';out.mkdir(parents=True,exist_ok=True)
digest=hashlib.sha256(code.encode()).hexdigest()
if not (out/'receipt.json').exists() or json.loads((out/'receipt.json').read_text()).get('sha256')!=digest:
    (out/'schema.cpp').write_text(code)
    argv=['g++','-std=c++17','-O0','-Wall','-Wextra','-I'+str(deps/'src'),str(out/'schema.cpp'),'-o',str(out/'validate')]
    subprocess.run(argv,check=True)
    (out/'receipt.json').write_text(json.dumps({'sha256':digest,'argv':argv,'production_bodies':['make_profile','guided_command Config::Object','from_json ConfString/ConfUint/ConfInt/ConfBool/ConfObject']},indent=2)+'\n')
print(out/'validate')
