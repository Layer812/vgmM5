#pragma once
// Pure metadata decode, only called during file-load, never in the audio hotpath.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
namespace tab5 {
struct TrackMeta {
  char title[192]={}, game[128]={}, system[96]={}, author[128]={}, release[48]={};
  char chips[128]={};
  uint32_t samples=0, loop_samples=0;
  unsigned tracks=0, pdx_samples=0;
  bool gd3=false, pdx_requested=false, pdx_loaded=false;
};
inline uint32_t meta_u32(const uint8_t* p){ return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
inline void meta_copy(char* dest, std::size_t cap, const std::string& value){
  if(!cap)return;
  // Do not split UTF-8 codepoints on the truncation boundary.
  std::size_t n=value.size()<cap-1?value.size():cap-1;
  while(n>0 && n<value.size() && ((uint8_t)value[n]&0xC0)==0x80)--n;
  memcpy(dest,value.data(),n);dest[n]=0;
}
inline void meta_utf8(std::string& s, uint32_t c){
  if(c<0x80)s+=(char)c;
  else if(c<0x800){s+=(char)(0xC0|(c>>6));s+=(char)(0x80|(c&63));}
  else if(c<0x10000){s+=(char)(0xE0|(c>>12));s+=(char)(0x80|((c>>6)&63));s+=(char)(0x80|(c&63));}
  else{s+=(char)(0xF0|(c>>18));s+=(char)(0x80|((c>>12)&63));s+=(char)(0x80|((c>>6)&63));s+=(char)(0x80|(c&63));}
}
inline bool parse_gd3(TrackMeta& result,const uint8_t* bytes,std::size_t n){
  if(n<12 || memcmp(bytes,"Gd3 ",4)!=0)return false;
  const uint32_t stated=meta_u32(bytes+8);
  if(stated>n-12)return false; // truncated tag must never be displayed as if complete
  std::string fields[11];unsigned field=0;
  for(std::size_t i=12;i+1<12+(std::size_t)stated && field<11;i+=2){
    uint32_t c=uint32_t(bytes[i])|(uint32_t(bytes[i+1])<<8);
    if(c==0){++field;continue;}
    if(c>=0xD800 && c<=0xDBFF && i+3<12+(std::size_t)stated){
      const uint32_t lo=uint32_t(bytes[i+2])|(uint32_t(bytes[i+3])<<8);
      if(lo>=0xDC00 && lo<=0xDFFF){c=0x10000+((c-0xD800)<<10)+(lo-0xDC00);i+=2;}
      else c=0xFFFD;
    }else if(c>=0xD800 && c<=0xDFFF)c=0xFFFD;
    if(fields[field].size()<512)meta_utf8(fields[field],c);
  }
  meta_copy(result.title,sizeof(result.title),fields[0].empty()?fields[1]:fields[0]);
  meta_copy(result.game,sizeof(result.game),fields[2].empty()?fields[3]:fields[2]);
  meta_copy(result.system,sizeof(result.system),fields[4].empty()?fields[5]:fields[4]);
  meta_copy(result.author,sizeof(result.author),fields[6].empty()?fields[7]:fields[6]);
  meta_copy(result.release,sizeof(result.release),fields[8]);
  result.gd3=true;return true;
}
} // namespace tab5
