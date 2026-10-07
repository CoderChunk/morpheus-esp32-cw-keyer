#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "keyer_settings_protocol.h"
int main(){int value=-1;
 assert(keyerProtocolValue("{\"value\":40}",&value)&&value==40);
 assert(keyerProtocolValue("{\"value\" : 40}",&value)&&value==40);
 assert(keyerProtocolValue("{\"value\": 2000,\"field\":\"tone\"}",&value)&&value==2000);
 for(const char* invalid:{"{\"value\":-1}","{\"value\":true}","{\"value\":1.5}","{\"value\":99999}","{\"value\":1junk}","{}"})assert(!keyerProtocolValue(invalid,&value));
 assert(keyerProtocolValid("wpm",5));assert(keyerProtocolValid("wpm",40));assert(!keyerProtocolValid("wpm",41));
 assert(keyerProtocolValid("tone",200));assert(keyerProtocolValid("tone",2000));assert(!keyerProtocolValid("tone",199));
 assert(keyerProtocolValid("reversed",0));assert(!keyerProtocolValid("mode",2));assert(!keyerProtocolValid("unknown",1));
 assert(keyerProtocolValid("weight",30));assert(!keyerProtocolValid("weight",71));
 puts("PASS: keyer setting parsing and ranges");
}
