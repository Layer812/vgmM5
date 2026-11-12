#pragma once
// Cardputer R67 SD thumbnail cache: / .VGMM5 / 8-digit FNV-1a.RGB
// Cache is raw 64x64 RGB565 little-endian (8192B). It is never held in RAM.
// At directory entry the browser downsizes ordinary PNG/JPEG covers once; player streams 4 lines at a time.
// No image decode, album scan or sprite allocation occurs during cursor movement or playback.
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
static constexpr const char* kCacheDir="/VGMM5";

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
  const int n=snprintf(target,len,"/VGMM5/%08lX.%s",
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
// A cache miss is repaired only at directory entry, never from draw().
// This handles small opaque PNG covers supplied by users without requiring
// a Windows conversion step. Large images stay on the PC-side converter path.
// No openNextFile(), album enumeration or music file opening occurs here.
static inline bool small_opaque_png(const char* candidate) {
  if(!candidate||candidate[0]!='/')return false;
  File file=SD.open(candidate,FILE_READ);
  if(!file||file.isDirectory()||file.size()<33u||file.size()>65536u) {
    if(file)file.close();
    return false;
  }
  uint8_t hdr[26]={};
  const bool read_ok=file.read(hdr,sizeof(hdr))==(int)sizeof(hdr);
  file.close();
  const uint8_t png_magic[8]={137,80,78,71,13,10,26,10};
  if(!read_ok||memcmp(hdr,png_magic,sizeof(png_magic))!=0||
     memcmp(hdr+12,"IHDR",4)!=0)return false;
  const uint32_t w=((uint32_t)hdr[16]<<24)|((uint32_t)hdr[17]<<16)|
                   ((uint32_t)hdr[18]<<8)|hdr[19];
  const uint32_t h=((uint32_t)hdr[20]<<24)|((uint32_t)hdr[21]<<16)|
                   ((uint32_t)hdr[22]<<8)|hdr[23];
  return w==64&&h==64&&hdr[25]==2; // RGB without alpha
}

static inline bool artwork_ext(const char* path,const char* ext) {
  if(!path||!ext)return false;
  const char* dot=strrchr(path,'.');
  if(!dot)return false;
  while(*dot&&*ext) {
    char a=*dot++, b=*ext++;
    if(a>='A'&&a<='Z')a=(char)(a+32);
    if(b>='A'&&b<='Z')b=(char)(b+32);
    if(a!=b)return false;
  }
  return !*dot&&!*ext;
}
// Only at folder entry: inspect headers before asking the PNG/JPEG decoder
// to downscale into the 8-KiB sprite. Do not allocate a full-size bitmap.
static inline bool artwork_dimensions(const char* path,int& width,int& height,bool& png) {
  png=artwork_ext(path,".png");
  const bool jpg=artwork_ext(path,".jpg")||artwork_ext(path,".jpeg");
  if(!png&&!jpg)return false;
  File f=SD.open(path,FILE_READ);
  if(!f||f.isDirectory()||f.size()<26 || f.size()>4194304u) {
    if(f)f.close();
    return false;
  }
  if(png) {
    uint8_t hdr[26]={};
    const bool ok=f.read(hdr,sizeof(hdr))==(int)sizeof(hdr);
    f.close();
    const uint8_t magic[8]={137,80,78,71,13,10,26,10};
    if(!ok||memcmp(hdr,magic,8)!=0||memcmp(hdr+12,"IHDR",4)!=0)return false;
    width=(int)(((uint32_t)hdr[16]<<24)|((uint32_t)hdr[17]<<16)|
                ((uint32_t)hdr[18]<<8)|hdr[19]);
    height=(int)(((uint32_t)hdr[20]<<24)|((uint32_t)hdr[21]<<16)|
                 ((uint32_t)hdr[22]<<8)|hdr[23]);
  } else {
    if(f.read()!=0xFF||f.read()!=0xD8){f.close();return false;}
    bool found=false;
    while(f.available()>8&&f.position()<16384u) {
      int marker=f.read();
      if(marker!=0xFF)continue;
      do{marker=f.read();}while(marker==0xFF&&f.available());
      if(marker<0||marker==0xD9||marker==0xDA)break;
      if(marker==0x01||(marker>=0xD0&&marker<=0xD7))continue;
      const int hi=f.read(),lo=f.read();
      if(hi<0||lo<0)break;
      const int len=(hi<<8)|lo;
      if(len<2||(uint32_t)f.position()+(uint32_t)len-2u>f.size())break;
      if((marker>=0xC0&&marker<=0xC3)||
         (marker>=0xC5&&marker<=0xC7)||
         (marker>=0xC9&&marker<=0xCB)||
         (marker>=0xCD&&marker<=0xCF)) {
        if(len<7)break;
        f.read(); // sample precision
        height=(f.read()<<8)|f.read();
        width=(f.read()<<8)|f.read();
        found=true;break;
      }
      f.seek(f.position()+(size_t)len-2u);
    }
    f.close();
    if(!found)return false;
  }
  return width>=1&&height>=1&&width<=4096&&height<=4096;
}

static inline bool write_small_cover(const char* source,const char* target,
                                     const char* album) {
  int width=0,height=0;
  bool is_png=false;
  if(!artwork_dimensions(source,width,height,is_png))return false;
  auto& lcd=M5Cardputer.Display;
  LGFX_Sprite sprite(&lcd);
  sprite.setColorDepth(16);
  if(!sprite.createSprite(64,64))return false;
  sprite.fillSprite(0x0842);
  const int longest=width>height?width:height;
  const float scale=longest>64?64.0f/(float)longest:1.0f;
  const int w=(int)(width*scale),h=(int)(height*scale);
  const int cx=(64-w)/2,cy=(64-h)/2;
  // The 64x64 path stays exact; ordinary covers are proportionally scaled.
  const bool decoded = is_png
    ? ((width==64&&height==64)
       ? sprite.drawPngFile(SD,source,0,0,64,64,0,0,1.0f)
       : sprite.drawPngFile(SD,source,cx,cy,64,64,0,0,scale))
    : sprite.drawJpgFile(SD,source,cx,cy,64,64,0,0,scale);
  if(!decoded) {
    sprite.deleteSprite();
    Serial.printf("[ART] Decode failed: %s\n",source);
    return false;
  }
  if(!SD.exists(kCacheDir)&&!SD.mkdir(kCacheDir)) {
    sprite.deleteSprite();
    return false;
  }
  char tmp[40]={};
  if(!path_for_hash(album,tmp,sizeof(tmp),"TMP")) {
    sprite.deleteSprite();
    return false;
  }
  SD.remove(tmp);
  File out=SD.open(tmp,FILE_WRITE);
  if(!out) {sprite.deleteSprite();return false;}
  uint16_t line[64];
  bool written=true;
  for(int y=0;y<64 && written;++y) {
    for(int x=0;x<64;++x)line[x]=sprite.readPixel(x,y);
    if(out.write((const uint8_t*)line,sizeof(line))!=sizeof(line))
      written=false;
  }
  out.flush();
  out.close();
  sprite.deleteSprite(); // 8 KiB image buffer released before audio playback.
  if(!written){SD.remove(tmp);return false;}
  File probe=SD.open(tmp,FILE_READ);
  const bool complete=probe && !probe.isDirectory() &&
                      probe.size()==kCacheBytes;
  if(probe)probe.close();
  if(!complete){SD.remove(tmp);return false;}
  // An existing valid cache is always authoritative; only replace invalid ones.
  File old=SD.open(target,FILE_READ);
  const bool exists_valid=old && !old.isDirectory() &&
                          old.size()==kCacheBytes;
  if(old)old.close();
  if(exists_valid) {SD.remove(tmp);return true;}
  SD.remove(target);
  if(!SD.rename(tmp,target)) {SD.remove(tmp);return false;}
  return true;
}
// Called once on entering a directory, not when moving the cursor.
// The hint is collected during the existing music directory enumeration.
static inline void prepare_album_once(const char* album,const char* image_hint=nullptr) {
  if(!album||album[0]!='/')return;
  char cache[40]={};
  if(!path_for_hash(album,cache,sizeof(cache)))return;
  File found=SD.open(cache,FILE_READ);
  const bool has_cache=found&&!found.isDirectory()&&
                       found.size()==kCacheBytes;
  if(found)found.close();
  if(has_cache)return;

  char candidate[320]={};
  // Known album-cover filenames have priority over arbitrary PNG/JPEG.
  const char* covers[]={"cover64.png","cover_64.png","cover.png",
      "Cover.png","COVER.PNG","folder.png","Folder.png","FOLDER.PNG",
      "cover.jpg","Cover.jpg","COVER.JPG","folder.jpg","Folder.jpg",
      "cover.jpeg","folder.jpeg"};
  for(const char* leaf:covers) {
    const int n=snprintf(candidate,sizeof(candidate),"%s%s%s",album,
                        strcmp(album,"/")==0?"":"/",leaf);
    if(n>0&&(size_t)n<sizeof(candidate)&&
       write_small_cover(candidate,cache,album))return;
  }
  const char* slash=strrchr(album,'/');
  const char* name=slash?slash+1:album;
  if(name[0]) {
    const char* extensions[]={".png",".PNG",".jpg",".JPG",".jpeg",".JPEG"};
    for(const char* ext:extensions) {
      const int n=snprintf(candidate,sizeof(candidate),"%s/%s%s",album,name,ext);
      if(n>0&&(size_t)n<sizeof(candidate)&&
         write_small_cover(candidate,cache,album))return;
    }
    // A cover named after its album may be beside the folder.
    char parent[256]={};
    snprintf(parent,sizeof(parent),"%s",album);
    char* pslash=strrchr(parent,'/');
    if(pslash) {
      if(pslash==parent)parent[1]=0;else *pslash=0;
      for(const char* ext:extensions) {
        const int n=snprintf(candidate,sizeof(candidate),"%s%s%s%s",
                             parent,strcmp(parent,"/")==0?"":"/",name,ext);
        if(n>0&&(size_t)n<sizeof(candidate)&&
           write_small_cover(candidate,cache,album))return;
      }
    }
  }
  // Every album can use an arbitrary PNG/JPEG name. The hint comes from
  // the existing one-pass SD enumeration; we never scan again here.
  if(image_hint&&image_hint[0]) {
    const char* leaf=strrchr(image_hint,'/');
    leaf=leaf?leaf+1:image_hint;
    if(leaf[0] && strcmp(leaf,".")!=0 && strcmp(leaf,"..")!=0) {
      const int n=snprintf(candidate,sizeof(candidate),"%s%s%s",album,
                          strcmp(album,"/")==0?"":"/",leaf);
      if(n>0&&(size_t)n<sizeof(candidate))
        write_small_cover(candidate,cache,album);
    }
  }
}

// Cursor movement only reads the deterministic hashed cache; preparation
// above runs solely when the user first ENTERS an album directory.
// Large PNG/JPEG covers must be converted once using Windows tool.
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
