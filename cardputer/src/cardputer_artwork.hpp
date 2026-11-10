#pragma once
// Cardputer R67 SD thumbnail cache: / .VGMM5 / 8-digit FNV-1a.RGB
// Cache is raw 64x64 RGB565 little-endian (8192B). It is never held in RAM.
// Browser may convert a small RGB PNG once; player only streams 4 lines at a time.
// Large "<folder>.png" files are NEVER decoded on cursor movement.
// No changes to audio, partition layout, network or music.
#if defined(IS_CARDPUTER)
#include <SD.h>
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
// Cursor movement: the album name is hashed and exactly one precomputed
// /.VGMM5/XXXXXXXX.RGB path is opened. NO folder enumeration, image probing,
// SD exists-checks, PNG decode, sprite, or one-time conversion run on-device.
// All album PNG/JPEG conversion is performed on Windows by
// tools/MAKE_CARDPUTER_THUMBS.ps1 before inserting the card.
static inline uint32_t album_key(const char* fullpath,Kind kind) {
  char folder[256]={};
  return folder_for(fullpath,kind,folder,sizeof(folder)) ?
         hash_album(folder) : 0;
}
static inline void draw(const char* fullpath,Kind kind,int x,int y,int size) {
  if(size!=64)return;
  auto& lcd=M5Cardputer.Display;
  lcd.fillRoundRect(x-2,y-2,68,68,4,0x09C4);
  char album[256]={};
  char filename[40]={};
  bool shown=false;
  // The playback view and browser both use exactly this hash-based lookup.
  if(kind!=PARENT && folder_for(fullpath,kind,album,sizeof(album)) &&
     path_for_hash(album,filename,sizeof(filename))) {
    shown=stream_cache(filename,x,y);
  }
  if(!shown)default_cover(album[0]?album:fullpath,kind,x,y);
  lcd.drawRoundRect(x-2,y-2,68,68,4,0x35AA);
}
} // namespace cardputer_artwork
#endif
