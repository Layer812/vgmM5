#pragma once
// Cardputer R67 artwork shared by the microSD browser and now-playing view.
// No cloud DB and no network requests: sidecar JPEG/PNG or an always-visible
// deterministic local cover tile. Designed for the 240x135 Cardputer display.
#if defined(IS_CARDPUTER)
#include <M5Cardputer.h>
#include <SD.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace cardputer_artwork {
enum Kind : uint8_t { FOLDER=0, VGM=1, MDX=2, PARENT=3 };

static inline uint32_t hash_path(const char* text) {
  uint32_t result=2166136261u;
  if (text) for (const uint8_t* p=(const uint8_t*)text; *p; ++p) {
    result=(result ^ *p)*16777619u;
  }
  return result;
}
static inline bool ext_is(const char* name,const char* expected) {
  if(!name||!expected)return false;
  const char* dot=strrchr(name,'.');
  if(!dot)return false;
  while(*dot&&*expected) {
    char a=*dot++,b=*expected++;
    if(a>='A'&&a<='Z')a+=32;
    if(b>='A'&&b<='Z')b+=32;
    if(a!=b)return false;
  }
  return !*dot && !*expected;
}
static inline bool exists_regular(const char* path) {
  if(!path||!SD.exists(path))return false;
  File file=SD.open(path,FILE_READ);
  const bool ok=file&&!file.isDirectory()&&file.size()>0
                &&file.size()<=2u*1024u*1024u;
  if(file)file.close();
  return ok;
}
static inline bool try_candidate(const char* path,char* out,size_t cap) {
  if(!out||cap==0||!path||!exists_regular(path))return false;
  const int n=snprintf(out,cap,"%s",path);
  return n>0 && (size_t)n<cap;
}

// Find JPG/PNG with the file's own name, then directory cover/folder art.
// Sibling art is optional. Neither the image nor any metadata DB is required.
static inline bool resolve(const char* fullpath,Kind kind,char* out,size_t cap) {
  if(!fullpath||!out||cap<8)return false;
  out[0]=0;
  if(kind==PARENT)return false;

  char stem[320]={};
  char path[320]={};
  const int n=snprintf(stem,sizeof(stem),"%s",fullpath);
  if(n<=0||(size_t)n>=sizeof(stem))return false;

  if(kind!=FOLDER) {
    char* slash=strrchr(stem,'/');
    char* dot=strrchr(stem,'.');
    if(dot && (!slash||dot>slash))*dot=0;
    const char* extensions[]={".jpg",".JPG",".jpeg",".JPEG",".png",".PNG"};
    for(const char* ext:extensions) {
      const int count=snprintf(path,sizeof(path),"%s%s",stem,ext);
      if(count>0&&(size_t)count<sizeof(path)&&try_candidate(path,out,cap))return true;
    }
  }
  const char* cover_names[]={"cover.jpg","Cover.jpg","COVER.JPG",
                              "cover.jpeg","cover.png","Cover.png",
                              "folder.jpg","Folder.jpg","folder.png"};
  char directory[320]={};
  if(kind==FOLDER) {
    const int count=snprintf(directory,sizeof(directory),"%s",fullpath);
    if(count<=0||(size_t)count>=sizeof(directory))return false;
  } else {
    const char* slash=strrchr(fullpath,'/');
    if(!slash)return false;
    const size_t size=slash==fullpath?1u:(size_t)(slash-fullpath);
    if(size>=sizeof(directory))return false;
    memcpy(directory,fullpath,size);
    directory[size]=0;
  }
  const size_t size=strlen(directory);
  for(const char* name:cover_names) {
    const int count=snprintf(path,sizeof(path),"%s%s%s",directory,
                             size && directory[size-1]=='/'?"":"/",name);
    if(count>0&&(size_t)count<sizeof(path)&&try_candidate(path,out,cap))return true;
  }
  return false;
}

// Read the JPEG's SOF marker to choose an appropriate downscale for covers
// supplied as 128/256/512/1024px instead of only 64x64 thumbnails.
static inline bool jpg_dimensions(const char* path,int& width,int& height) {
  File f=SD.open(path,FILE_READ);
  if(!f)return false;
  if(f.read()!=0xFF||f.read()!=0xD8){f.close();return false;}
  while(f.available()>8 && f.position()<16384u) {
    int m=f.read();
    if(m!=0xFF)continue;
    do{m=f.read();}while(m==0xFF&&f.available());
    if(m<0||m==0xD9||m==0xDA)break;
    if(m==0x01||(m>=0xD0&&m<=0xD7))continue;
    const int hi=f.read(),lo=f.read();
    if(hi<0||lo<0)break;
    const int len=(hi<<8)|lo;
    if(len<2 || (uint32_t)f.position()+(uint32_t)len-2u>f.size())break;
    if((m>=0xC0&&m<=0xC3)||(m>=0xC5&&m<=0xC7)||
       (m>=0xC9&&m<=0xCB)||(m>=0xCD&&m<=0xCF)) {
      if(len<7)break;
      f.read(); // sample precision
      height=(f.read()<<8)|f.read();
      width=(f.read()<<8)|f.read();
      f.close();
      return width>0&&height>0;
    }
    f.seek(f.position()+(size_t)len-2u);
  }
  f.close();
  return false;
}
static inline bool draw_real(const char* path,int x,int y,int side) {
  if(!path||!exists_regular(path))return false;
  auto& lcd=M5Cardputer.Display;
  if(ext_is(path,".png")){
    return lcd.drawPngFile(SD,path,x,y,side,side,0,0,1.0f);
  }
  int width=side,height=side;
  float scale=1.0f;
  if(jpg_dimensions(path,width,height)) {
    const int largest=width>height?width:height;
    if(largest>side)scale=(float)side/(float)largest;
  }
  return lcd.drawJpgFile(SD,path,x,y,side,side,0,0,scale);
}

// Always draws a proper colorful thumbnail even with no matching cover.jpg.
// This makes the browser useful on a plain microSD card full of VGM/MDX files.
static inline void draw_default(const char* track,Kind kind,int x,int y,int side) {
  auto& lcd=M5Cardputer.Display;
  const uint32_t seed=hash_path(track);
  const uint16_t palette[]={0x1595,0x32B2,0x29AA,0x6264,0x492C,0x3BC5};
  const uint16_t accent=palette[(seed>>4)%6u];
  const uint16_t base=(kind==FOLDER||kind==PARENT)?0x012C:0x0842;
  lcd.fillRoundRect(x,y,side,side,3,base);
  for(int i=0;i<8;i++) {
    const int t=(int)((seed>>(i*3u%23u))&7u);
    lcd.drawLine(x+(side*i)/8,y,x+(side*(i+2))/8,
                 y+side-1,(i&1)?accent:0x22D2);
    if(t&2)lcd.drawFastHLine(x+3,y+4+i*(side-9)/9,side-6,0x11A8);
  }
  if(kind==FOLDER||kind==PARENT) {
    lcd.fillRoundRect(x+12,y+21,side-24,side/2,3,0xFCC0);
    lcd.fillRect(x+12,y+17,side/3,8,0xFDC0);
    lcd.drawFastHLine(x+16,y+32,side-32,0xEE00);
  } else {
    // PCM waveform / musical bars: not a text-only "VGM" placeholder.
    for(int i=0;i<7;i++) {
      const int level=8+(int)((seed>>(i*4u%23u))&15u);
      lcd.fillRect(x+11+i*(side-20)/7,y+side/2-level/2,4,level,
                   i&1?0xFFE0:0x5FFE);
    }
    lcd.fillCircle(x+side/2,y+side/3,5,0xFFFF);
  }
  lcd.fillRect(x+3,y+side-18,side-6,15,0x0020);
  lcd.setTextSize(1.5f);
  lcd.setTextColor(kind==FOLDER||kind==PARENT?0xFFE0:0xAFE9);
  lcd.drawCenterString(kind==FOLDER?"DIR":kind==PARENT?"../":
                        kind==MDX?"MDX":"VGM",x+side/2,y+side-18);
}
static inline void draw(const char* fullpath,Kind kind,
                        int x,int y,int side) {
  auto& lcd=M5Cardputer.Display;
  if(side<16)return;
  lcd.fillRoundRect(x-2,y-2,side+4,side+4,4,0x09C4);
  char candidate[320]={};
  bool rendered=false;
  if(resolve(fullpath,kind,candidate,sizeof(candidate))) {
    rendered=draw_real(candidate,x,y,side);
    if(!rendered)Serial.printf("Cardputer cover decode failed: %s\n",candidate);
  }
  if(!rendered)draw_default(fullpath,kind,x,y,side);
  lcd.drawRoundRect(x-2,y-2,side+4,side+4,4,0x35AA);
}
} // namespace cardputer_artwork
#endif
