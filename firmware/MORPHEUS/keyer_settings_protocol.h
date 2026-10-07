#ifndef MORPHEUS_KEYER_SETTINGS_PROTOCOL_H
#define MORPHEUS_KEYER_SETTINGS_PROTOCOL_H
#include <cstring>
// Bounded bare integer; reject truncation, signs, decimals and trailing junk.
inline bool keyerProtocolValue(const char* json,int* result) {
  const char* p=strstr(json,"\"value\"");if(!p)return false;p+=7;
  while(*p==' '||*p=='\t')++p;
  if(*p++!=':')return false;
  while(*p==' '||*p=='\t')++p;
  if(*p<'0'||*p>'9')return false;
  int value=0,digits=0;
  while(*p>='0'&&*p<='9'){if(++digits>4)return false;value=value*10+(*p++-'0');}
  while(*p==' '||*p=='\t')++p;
  if(*p!=','&&*p!='}')return false;
  *result=value;return true;
}
inline bool keyerProtocolValid(const char* field,int value) {
  if(!strcmp(field,"wpm"))return value>=5&&value<=40;
  if(!strcmp(field,"tone"))return value>=200&&value<=2000;
  if(!strcmp(field,"volume"))return value>=0&&value<=100;
  if(!strcmp(field,"weight"))return value>=30&&value<=70;
  if(!strcmp(field,"mode")||!strcmp(field,"iambic")||!strcmp(field,"reversed")||!strcmp(field,"sidetone"))return value==0||value==1;
  return false;
}
#endif
