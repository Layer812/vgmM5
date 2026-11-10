#pragma once
// Cardputer R67 SD thumbnail cache: / .VGMM5 / 8-digit FNV-1a.RGB
// Cache is raw 64x64 RGB565 little-endian (8192B). It is never held in RAM.
// Browser may convert a small RGB PNG once; player only streams 4 lines at a time.
// Large "<folder>.png" files are NEVER decoded on cursor movement.
// No changes to audio, partition layout, network or music.
#if defined(IS_CARDPUTER)
#include <SD.h> // before M5Cardputer/M5GFX for fs::SDFS image file support
#include <M5Cardputer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace cardputer_artwork {
enum Kind : uint8_t { FOLDER=0, VGM=1, MDX=2, PARENT=3 };
static constexpr int kPixels=64;
static constexpr size_t kCacheBytes=64u*64u*2u;
static constexpr const char* kCacheDir="/.VGMM5";

static inline bool folder_for(const char* filepath,Kind kind,char* out,size_t cap) {
  if(!filepath||filepath[0]!='/'||!out||cap<3)return false;
  const int n=snprintf(out,cap,"%s",filepath);
  if(n<=0||(size_t)n>=cap)return false;
  if(kind!=FOLDER) {
    char* slash=strrchr(out,'/');
    if(!slash)return false;
    if(slash==out)out[1]=0;else *slash=0;
  }
  size_t len=strlen(out);
  while(len>1 && out[len-1]=='/')out[--len]=0;
  return true;
}
static inline uint32_t hash_album(const char* album) {
  uint32_t hash=2166136261u;
  if(album)for(const uint8_t* ptr=(const uint8_t*)album;*ptr;++ptr) {
    uint8_t c=*ptr=='\\'?'/':*ptr;
    if(c>='A'&&c<='Z')c=(uint8_t)(c+32);
    hash=(hash^c)*16777619u;
  }
  return hash;
}
static inline bool path_for_hash(const char* album,char* target,size_t len,
                                 const char* ext="RGB") {
  if(!album||!target||len<25)return false;
  const int n=snprintf(target,len,"/.VGMM5/%08lX.%s",
                       (unsigned long)hash_album(album),ext);
  return n>0&&(size_t)n<len;
}
// 16 chunks x 512 bytes: playback uses only 512 bytes of thumbnail stack.
static inline bool stream_cache(const char* filename,int x,int y) {
  File file=SD.open(filename,FILE_READ);
  if(!file||file.isDirectory()||file.size()!=kCacheBytes) {
    if(file)file.close();
    return false;
  }
  uint16_t strip[64*4];
  bool ok=true;
  for(int py=0;py<64;py+=4) {
    if(file.read((uint8_t*)strip,sizeof(strip))!=(int)sizeof(strip)) {
      ok=false;break;
    }
    M5Cardputer.Display.pushImage(x,y+py,64,4,strip);
  }
  file.close();
  return ok;
}
static inline void default_cover(const char* album,Kind kind,int x,int y) {
  auto& screen=M5Cardputer.Display;
  const uint32_t h=hash_album(album);
  const uint16_t colors[]={0x1595,0x32B2,0x29AA,0x6264,0x492C,0x3BC5};
  screen.fillRoundRect(x,y,64,64,4,0x0842);
  for(int i=0;i<7;++i) {
    const int bar=8+((h>>(i*3u%22u))&23);
    screen.fillRect(x+8+i*7,y+33-bar/2,4,bar,(i&1)?colors[(h>>5)%6]:0x5FFE);
  }
  screen.fillRect(x+3,y+45,58,16,0x0020);
  screen.setTextSize(1.5f);
  screen.setTextColor((kind==FOLDER||kind==PARENT)?CYAN:0xAFE9);
  screen.drawCenterString(kind==FOLDER?"DIR":kind==PARENT?"../":
                          kind==MDX?"MDX":"VGM",x+32,y+46);
}
// Avoid scanning image directories for each highlighted file.
// Only recognized, small 64x64 RGB PNGs may be decoded once on-device.
// Everything larger must be converted beforehand using the Windows helper.
static inline bool small_png(const char* album,char* filename,size_t cap) {
  const char* const names[]={"cover64.png","cover_64.png","thumb.png",
                              "cover.png","Cover.png","COVER.PNG"};
  char candidate[320];
  for(const char* name:names) {
    const int n=snprintf(candidate,sizeof(candidate),"%s%s%s",album,
                         strcmp(album,"/")==0?"":"/",name);
    if(n<=0||(size_t)n>=sizeof(candidate))continue;
    File f=SD.open(candidate,FILE_READ);
    if(!f||f.isDirectory()||f.size()>65536u||f.size()<33u) {
      if(f)f.close();continue;
    }
    uint8_t hdr[26]={};
    const bool valid=f.read(hdr,sizeof(hdr))==(int)sizeof(hdr);
    f.close();
    static const uint8_t png[8]={137,80,78,71,13,10,26,10};
    if(!valid||memcmp(hdr,png,8)!=0||memcmp(hdr+12,"IHDR",4)!=0)continue;
    const uint32_t w=((uint32_t)hdr[16]<<24)|((uint32_t)hdr[17]<<16)|
                     ((uint32_t)hdr[18]<<8)|hdr[19];
    const uint32_t h=((uint32_t)hdr[20]<<24)|((uint32_t)hdr[21]<<16)|
                     ((uint32_t)hdr[22]<<8)|hdr[23];
    if(w!=64||h!=64||hdr[25]!=2)continue; // only opaque RGB, not RGBA
    if((size_t)n<cap) {snprintf(filename,cap,"%s",candidate);return true;}
  }
  return false;
}
static inline bool generate_from_small_png(const char* album,
                                            const char* destination) {
  char source[320]={};
  if(!small_png(album,source,sizeof(source)))return false;
  LGFX_Sprite temp(&M5Cardputer.Display);
  temp.setColorDepth(16);
  if(!temp.createSprite(64,64))return false;
  temp.fillSprite(0x0842);
  const bool decoded=temp.drawPngFile(SD,source,0,0,64,64,0,0,1.0f);
  if(!decoded){temp.deleteSprite();return false;}
  if(!SD.exists(kCacheDir)&&!SD.mkdir(kCacheDir)){
    temp.deleteSprite();return false;
  }
  char tempname[40]={};
  if(!path_for_hash(album,tempname,sizeof(tempname),"TMP")){
    temp.deleteSprite();return false;
  }
  SD.remove(tempname);
  File out=SD.open(tempname,FILE_WRITE);
  if(!out){temp.deleteSprite();return false;}
  uint16_t line[64];
  bool good=true;
  for(int y=0;y<64&&good;++y) {
    for(int x=0;x<64;++x)line[x]=temp.readPixel(x,y);
    if(out.write((const uint8_t*)line,sizeof(line))!=sizeof(line))good=false;
  }
  out.flush();
  out.close();
  temp.deleteSprite(); // release 8KiB before any music playback
  if(!good){SD.remove(tempname);return false;}
  File verify=SD.open(tempname,FILE_READ);
  const bool complete=verify&&verify.size()==kCacheBytes;
  if(verify)verify.close();
  if(!complete){SD.remove(tempname);return false;}
  SD.remove(destination);
  if(!SD.rename(tempname,destination)){
    SD.remove(tempname);return false;
  }
  return true;
}
static inline void draw(const char* fullpath,Kind kind,int x,int y,int size,
                        bool allow_build=false) {
  if(size!=64)return;
  auto& lcd=M5Cardputer.Display;
  lcd.fillRoundRect(x-2,y-2,68,68,4,0x09C4);
  char album[320]={};
  char filename[40]={};
  bool shown=false;
  if(kind!=PARENT && folder_for(fullpath,kind,album,sizeof(album)) &&
     path_for_hash(album,filename,sizeof(filename))) {
    shown=stream_cache(filename,x,y);
    if(!shown&&allow_build&&generate_from_small_png(album,filename))
      shown=stream_cache(filename,x,y);
  }
  if(!shown)default_cover(album[0]?album:fullpath,kind,x,y);
  lcd.drawRoundRect(x-2,y-2,68,68,4,0x35AA);
}
} // namespace cardputer_artwork
#endif
