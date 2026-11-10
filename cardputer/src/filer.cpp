#if defined(IS_CARDPUTER)
#include <SD.h> // Must precede M5GFX/M5Cardputer for drawJpgFile(fs::FS,...)
#include <M5Cardputer.h>
#include <SPI.h>
#include <time.h>
// #include <ESP32-targz.h>
#include "filer.hpp"
#include "misakiSJIS.h"

int vol = 96;
bool loopflag = false;
bool menuflag = false;
bool playall = false;
bool playend = false;
char *gtitle = nullptr;
int sel = 0;
int disp = 0;
int pcmvol = 0;
int pcmskip = 0;


struct fl *filelist = nullptr;

int filenum = 0;
uint8_t cdir[PATHMAX] = "/";
uint8_t dirs[DIRMAX][PATHMAX] = {"/"};
int dirnum = 0;

const char statstr[STATNUM][STATMAX] = {"         ", "loading ", "playing  ", "play all "}; 
// ビットパターン表示
// d: 8ビットパターンデータ
//
void bitdisp(byte x, byte y, uint8_t d ) {
  for (byte i=0; i<8;i++) {
    if (d & 0x80>>i) {
      if(x + i < 232 && y < 64){
          M5.Display.drawPixel(x + i ,y , WHITE);
          M5.Display.drawPixel(x + i ,y + 1 , WHITE);
          M5.Display.drawPixel(x + i + 1 ,y , WHITE);
          M5.Display.drawPixel(x + i + 1 ,y + 1, WHITE);
      }
    }      
  }
}
// フォントパターンを画面に表示する
// 引数
// x,y 表示位置
//  pUTF8 表示する文字列
// ※半角文字は全角文字に置き換えを行う
//
void drawJPChar(byte x, byte y, char *data) {
  byte buf[60][8];  //160x8ドットのバナー表示パターン
  int n = 0;
  // バナー用パターン作成
  for (byte i=0; i < 60 && data[i] != 0; i++) {
    data = getFontData(&buf[i][0], data, false);  // フォントデータの取得    
    n++;
  }
  // ドット表示
  for (byte i=0; i < 8; i++) {
    for (byte j=0; j < n; j++){
        bitdisp(x * 2 + (j * 8) ,y * 2 + i , buf[j][i]);
    }
  }
}

/* void disptitle(int stat, char *title){
  M5Cardputer.Display.setTextSize(2.0f);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.fillRect(0, 0, 240, 34, BLACK);
  M5Cardputer.Display.setTextColor(OLIVE);
  M5Cardputer.Display.printf("vgmM5 %.1f ", VERSION);
  // M5Cardputer.Display.setTextColor(GREEN);
    // M5Cardputer.Display.printf("%s", statstr[stat % STATNUM]);
  M5Cardputer.Display.drawLine(0,33,240,33,OLIVE);
  if(title){
    M5Cardputer.Display.setCursor(0, 18);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.setTextSize(1.5f);
    M5Cardputer.Display.printf("%.*s", 26, title);
    M5Cardputer.Display.setTextSize(2);
  }
} */

// R67 browser help is a full-screen overlay. M toggles without losing cursor.
static bool browser_help_visible = false;

void dispmenu() {
  browser_help_visible = true;
  auto& lcd = M5Cardputer.Display;
  lcd.fillScreen(BLACK);
  lcd.setTextSize(2.0f);
  lcd.setTextColor(CYAN);
  lcd.drawString("KEY HELP", 5, 2);
  lcd.setTextSize(1.5f);
  lcd.setTextColor(GREEN);
  lcd.drawString("M:CLOSE", 180, 6);
  lcd.drawLine(0, 26, 239, 26, 0x2C68);
  lcd.setTextColor(WHITE);
  lcd.drawString("; / .    MOVE UP/DOWN", 7, 32);
  lcd.drawString(", / /    PAGE UP/DOWN", 7, 52);
  lcd.drawString("SPACE    PLAY / STOP", 7, 72);
  lcd.drawString("A ALL    B BACK", 7, 92);
  lcd.drawString("+/-      VOLUME", 7, 112);
}

void hitkey(){
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isKeyPressed(' ')){
      playend = true;
      playall = false;
     // printf("space pressed \n");
    }
    if (M5Cardputer.Keyboard.isKeyPressed('=')){
      int v = M5.Speaker.getVolume() + 20;
      if(v > 255) v = 255;
      M5.Speaker.setVolume(v);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('-')){
      int v = M5.Speaker.getVolume() - 20;
      if(v < 0) v = 0;
      M5.Speaker.setVolume(v);
    }
    if (M5Cardputer.Keyboard.isKeyPressed('7')){
      pcmskip -= 10;
      if(pcmskip < 200)
        pcmskip = 200;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('8')){
      pcmskip += 10;
      if(pcmskip > 400)
        pcmskip = 400;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('9')){
      pcmvol -= 20;
      if(pcmvol < -255)
        pcmvol = -255;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('0')){
      pcmvol += 20;
      if(pcmvol > 255)
        pcmvol = 255;
    }
    if (M5Cardputer.Keyboard.isKeyPressed('a')){
      playall = !playall;
      loopflag = false;
      disptitle(playall? STATALL: STATCLR, gtitle);
      //printf("a pressed \n");
    }
    if (M5Cardputer.Keyboard.isKeyPressed('m')){
      menuflag = !menuflag;
      if(menuflag)
        dispmenu();
      //printf("m pressed \n");
    }
    if (M5Cardputer.Keyboard.isKeyPressed('l')){
      loopflag = !loopflag;
      //printf("p pressed \n");
    }
  }
}

bool isvgmfile(const char *fileName) {
  int i; char exttmp[5];
  exttmp[4] = 0;
  const char *ext = strrchr(fileName, '.');
  if(ext == NULL)
    return false;
  if (strlen(ext) != 4) return false;
  for(i = 0; i < 4; i++)
    exttmp[i] = tolower((unsigned char)ext[i]);

  return (strcmp(".vgm", exttmp) == 0 || strcmp(".vgz", exttmp) == 0);
}

bool ismdxfile(const char *fileName) {
  int i; char exttmp[5];
  exttmp[4] = 0;
  const char *ext = strrchr(fileName, '.');
  if(ext == NULL)
    return false;
  if (strlen(ext) != 4) return false;
  for(i = 0; i < 4; i++)
    exttmp[i] = tolower((unsigned char)ext[i]);
  return (strcmp(".mdx", exttmp) == 0);
}

int makevgmlist(fs::FS &fs) {
  int i = 0;
  if (filelist == nullptr) {
      filelist = (struct fl *)malloc(sizeof(struct fl) * LISTMAX);
  }
  if (filelist == nullptr) return 0;
 // printf("ck1\n");
   //printf("ck6 %s\n", cdir);
  File root = fs.open((const char *)cdir);
  if (!root.isDirectory()) {
    return 0;
  }
  //printf("ck7 %d\n", dirnum);
  memset(filelist, 0, sizeof(struct fl) * LISTMAX);
  if(dirnum > 0){
    strcpy((char *)filelist[0].filename, (char *)dirs[dirnum - 1]);
    filelist[0].type = TYPE_UDIR;
    i++;
  }
  File file = root.openNextFile();
  while (file) {
 //   printf("ck8 %s\n", file.name());
    if (i == LISTMAX)
      break;
//    sprintf((char *)filelist[i].filename, "/%.*s", PATHMAX - 2, file.name());
//    printf("ck4 %d %s\n", i, filelist[i]);
    if (isvgmfile((const char *)file.name())){
      snprintf((char*)filelist[i].filename, PATHMAX, "%s", file.name());
      filelist[i].type = TYPE_VGM;
      i++;
    } else if (ismdxfile((const char *)file.name())){
      snprintf((char*)filelist[i].filename, PATHMAX, "%s", file.name());
      filelist[i].type = TYPE_MDX;
      i++;
    } else if(file.isDirectory()){
      const char* fname = file.name();
      if (strcmp(fname, "System Volume Information") != 0 && strcmp(fname, "/System Volume Information") != 0) {
        snprintf((char*)filelist[i].filename, PATHMAX, "%s", fname);
        filelist[i].type = TYPE_SDIR;
        i++;
      }
    }
//    printf("ck5 %d %s\n", i, filelist[i]);
    file = root.openNextFile();
//    printf("ck6\n");
  }
  return i;
}

// Actual Cardputer screen: 240x135 pixels.
// Header 0..26; list x=0..141 (5 x 21px rows); detail x=145..239.
// Do not use the bottom 19px for permanently visible help text.
static bool browser_make_path(char* out, size_t cap, const char* item) {
  if (!out || !cap || !item || !*item) return false;
  int written;
  if (*item == '/') written = snprintf(out, cap, "%s", item);
  else written = snprintf(out, cap, "%s%s%s", (const char*)cdir,
                          strcmp((const char*)cdir, "/")==0 ? "" : "/", item);
  return written >= 0 && (size_t)written < cap;
}

static bool browser_draw_jpg(const char* filename) {
  if (!filename || !SD.exists(filename)) return false;
  File image = SD.open(filename, FILE_READ);
  if (!image || image.isDirectory() || image.size()==0 || image.size() > 131072) {
    if (image) image.close();
    return false;
  }
  image.close();
  // Recommended 64x64 baseline JPG. Larger JPGs are cropped (not upscaled).
  M5Cardputer.Display.drawJpgFile(SD, filename, 160, 30, 64, 64);
  return true;
}

static void browser_thumbnail(const struct fl& entry, const char* fullpath,
                              const char* itemname) {
  auto& lcd = M5Cardputer.Display;
  const bool directory = entry.type==TYPE_SDIR || entry.type==TYPE_UDIR;
  lcd.fillRoundRect(159,29,66,66,4,0x0924);
  lcd.drawRoundRect(159,29,66,66,4,0x35AA);
  bool has_image = false;
  char imagepath[PATHMAX+16] = {};
  const char* name = itemname ? itemname : "";

  if (entry.type==TYPE_UDIR) {
    // Parent directory is represented by a slash, never an album cover.
  } else if (directory) {
    int n=snprintf(imagepath,sizeof(imagepath),"%s/cover.jpg", fullpath);
    if(n>0&&(size_t)n<sizeof(imagepath))has_image=browser_draw_jpg(imagepath);
  } else {
    // Song-specific cover: "song.vgm" -> "song.jpg" in the same folder.
    int n=snprintf(imagepath,sizeof(imagepath),"%s",fullpath);
    if(n>0&&(size_t)n<sizeof(imagepath)) {
      char* dot=strrchr(imagepath,'.');
      if(dot && (size_t)(dot-imagepath)+5 < sizeof(imagepath)) {
        strcpy(dot,".jpg");
        has_image=browser_draw_jpg(imagepath);
      }
    }
    if(!has_image) {
      // Shared folder cover used for all tracks in an album directory.
      n=snprintf(imagepath,sizeof(imagepath),"%s%scover.jpg",
                 (const char*)cdir,strcmp((const char*)cdir,"/")==0?"":"/");
      if(n>0&&(size_t)n<sizeof(imagepath))has_image=browser_draw_jpg(imagepath);
    }
  }
  if (has_image) return;

  // No artwork on SD: native vector thumbnail needs no DB/network/assets.
  lcd.fillRect(162,32,60,60,0x0924);
  lcd.setTextColor(directory?CYAN:0x6EE6);
  lcd.setTextSize(directory?3.0f:2.0f);
  lcd.drawCenterString(directory?"/":(entry.type==TYPE_MDX?"MDX":"VGM"),192,49);
  lcd.drawFastHLine(171,85,42,0x34E7);
  lcd.setTextSize(1.5f);
}

static void browser_draw_info(int selection) {
  auto& lcd = M5Cardputer.Display;
  lcd.fillRect(144,27,96,108,BLACK);
  lcd.drawFastVLine(143,27,108,0x2C68);
  if (selection < 0 || selection>=filenum || !filelist) return;
  const struct fl& e=filelist[selection];
  const bool directory=e.type==TYPE_SDIR || e.type==TYPE_UDIR;
  const char* stored=(const char*)e.filename;
  const char* shortname=strrchr(stored,'/');
  shortname = shortname&&shortname[1] ? shortname+1 : stored;
  char fullpath[PATHMAX+16] = {};
  if(!browser_make_path(fullpath,sizeof(fullpath),stored))return;
  browser_thumbnail(e,fullpath,shortname);

  lcd.setTextSize(1.5f);
  lcd.setTextColor(directory?CYAN:0x9FE7);
  const char* format=directory?"DIR":e.type==TYPE_MDX?"MDX":"VGM";
  String ext=String(strrchr(shortname,'.')?strrchr(shortname,'.'):"");
  ext.toLowerCase();
  if (!directory && ext==".vgz") format="VGZ";
  String metadata="";

  if(directory) {
    metadata = e.type==TYPE_UDIR?"PARENT /":"OPEN /";
  } else {
    File file=SD.open(fullpath, FILE_READ);
    if(file && !file.isDirectory()) {
      const uint32_t length=(uint32_t)file.size();
      char bytes[16];
      if(length>=1048576u)snprintf(bytes,sizeof(bytes),"%lu.%luM",
         (unsigned long)(length/1048576u),
         (unsigned long)((length%1048576u)*10u/1048576u));
      else snprintf(bytes,sizeof(bytes),"%luK",(unsigned long)((length+1023u)/1024u));
      metadata=bytes;
      if(ext==".vgm" && length>=0xb0u) {
        uint8_t header[0xb0]={};
        if(file.read(header,sizeof(header))==(int)sizeof(header) &&
           memcmp(header,"Vgm ",4)==0) {
          const bool fm=(header[0x30]||header[0x31]||header[0x32]||header[0x33]);
          const bool c140=(header[0xa8]||header[0xa9]||header[0xaa]||header[0xab]);
          if(fm&&c140)metadata += " OPM+C140";
          else if(fm)metadata += " YM2151";
          else if(c140)metadata += " C140";
        }
      }
    }
    if(file)file.close();
  }

  lcd.drawString(format,149,99);
  lcd.setTextColor(WHITE);
  // Format on first line, size / native chip info on second line.
  const String info=metadata.length() ? metadata : (directory?"FOLDER":"SD FILE");
  String shown=info;
  while(shown.length() && lcd.textWidth(shown)>87) {
    int cut=(int)shown.length()-1;
    while(cut>0 && (((uint8_t)shown[(unsigned)cut]&0xC0u)==0x80u))--cut;
    shown.remove((unsigned)cut);
  }
  lcd.drawString(shown,148,117);
}

void dispfiles(int offset, int chosen, bool start) {
  if(browser_help_visible)return;
  auto& lcd=M5Cardputer.Display;
  lcd.fillRect(0,27,143,108,BLACK);
  lcd.setTextSize(1.5f);
  lcd.setTextWrap(false);
  for(int row=0;row<DISPMAX&&row+offset<filenum;++row) {
    const int idx=row+offset;
    if(idx<0||!filelist)break;
    const int y=29+row*21;
    const bool focus=idx==chosen;
    const bool dir=filelist[idx].type==TYPE_SDIR ||
                   filelist[idx].type==TYPE_UDIR;
    if(focus)lcd.fillRoundRect(2,y,139,20,2,start?0x5BC0:0x03A0);
    lcd.setTextColor(focus?BLACK:(dir?CYAN:
                     filelist[idx].type==TYPE_MDX?YELLOW:WHITE));
    const char* name=(const char*)filelist[idx].filename;
    const char* slash=strrchr(name,'/');
    if(slash&&slash[1])name=slash+1;
    // Directories are cyan and end in "/" instead of a wide "[DIR]" label.
    String text=String(name);
    if(dir && (text.length()==0 || !text.endsWith("/")))text += "/";
    const int width=130;
    while(text.length() && lcd.textWidth(text)>width) {
      int cut=(int)text.length()-1;
      // Preserve folder "/" on truncated names.
      const bool suffix=dir&&text.endsWith("/");
      if(suffix) {
        text.remove(text.length()-1);
        cut=(int)text.length()-1;
      }
      while(cut>0 && (((uint8_t)text[(unsigned)cut]&0xC0u)==0x80u))--cut;
      text.remove((unsigned)cut);
      if(suffix)text += "/";
    }
    lcd.drawString(text,6,y+2);
  }
  browser_draw_info(chosen);
}

int selectfile(){
    if(filenum<0)filenum=0;
    if(filenum>0 && sel>=filenum)sel=filenum-1;
    if(sel<0)sel=0;
    if(disp>sel)disp=sel;
    if(filenum==0){
      M5Cardputer.Display.fillRect(0,35,240,100,BLACK);
      M5Cardputer.Display.setTextSize(1.5f);
      M5Cardputer.Display.setTextColor(YELLOW);
      M5Cardputer.Display.drawString("NO MUSIC FILES",6,46);
      M5Cardputer.Display.setTextColor(WHITE);
      M5Cardputer.Display.drawString("Add VGM/VGZ/MDX",6,72);
      M5Cardputer.Display.drawString("B: Rescan",6,100);
    }else{
      dispfiles(disp, sel, false);
    }
    if(playall){
      if(sel < (filenum - 1)){
        sel++;
        disp++;
        dispfiles(disp, sel, true);
        return sel;
      }else{
        playall = false;
        sel = disp = 0;
        dispfiles(disp, sel, false);
      }
    }
    while(1){
      delay(10); // leave CPU time for the synthesis/DMA tasks
      M5Cardputer.update();
      if (M5Cardputer.Keyboard.isChange()) {
          if(filenum == 0){
              if(M5Cardputer.Keyboard.isKeyPressed('b') ||
                 M5Cardputer.Keyboard.isKeyPressed('`')) return -1;
              continue; // never index filelist when the folder is empty
          }
          if (M5Cardputer.Keyboard.isKeyPressed(';')){
            if(sel != 0){
              if(sel == disp)
                disp--;
              sel--;
            }
            dispfiles(disp, sel, false);
          }
          if (M5Cardputer.Keyboard.isKeyPressed('.')){
            if((sel - disp) < (DISPMAX - 1)){
              sel++;
            }else{
              if(sel < (filenum - 1)){
                sel++;
                disp++;
              }
            }
            dispfiles(disp, sel, false);
          }
          if (M5Cardputer.Keyboard.isKeyPressed(',')){
            if(sel - DISPMAX > 0){
              sel -= DISPMAX;
              disp = sel;
            }else{
              sel = disp = 0;
            }
            dispfiles(disp, sel, false);
          }
          if (M5Cardputer.Keyboard.isKeyPressed('/')){
             if(sel < filenum - DISPMAX){
              sel += DISPMAX;
              disp += DISPMAX;
            }
            dispfiles(disp, sel, false);
          }
          if (M5Cardputer.Keyboard.isKeyPressed(' ') && filenum>0){
            dispfiles(disp, sel, true);
            return sel;
          }
          if (M5Cardputer.Keyboard.isKeyPressed('') || M5Cardputer.Keyboard.isKeyPressed('`') || M5Cardputer.Keyboard.isKeyPressed('b')){
            // Exit local mode
            return -1;
          }
          if (M5Cardputer.Keyboard.isKeyPressed('=')){
            int v = M5.Speaker.getVolume() + 20;
            if(v > 255) v = 255;
            M5.Speaker.setVolume(v);
          }
          if (M5Cardputer.Keyboard.isKeyPressed('-')){
            int v = M5.Speaker.getVolume() - 20;
            if(v < 0) v = 0;
            M5.Speaker.setVolume(v);
          }       
          if (M5Cardputer.Keyboard.isKeyPressed('m')){
            dispmenu();
            //printf("m pressed \n");
          }
          if (M5Cardputer.Keyboard.isKeyPressed('a')){
            if(filelist[sel].type != TYPE_VGM && filelist[sel].type != TYPE_MDX)
              continue;
            playall = true;
            loopflag = false;
            disptitle(playall? STATALL: STATCLR, gtitle);
            return sel;
          }
      }
    }
}

bool cnvfile(fs::FS &fs, struct fl *srct, uint8_t *dst){
  bool ret = true;
  //printf("ck4 %d\n", srct->type);
  switch(srct->type){
    case TYPE_VGM:
    case TYPE_MDX:
      if (srct->filename[0] == '/') {
          strcpy((char *)dst, (char *)srct->filename);
      } else {
          sprintf((char *)dst, "%s%s%s", cdir, (strcmp((char*)cdir, "/") == 0)?"":"/",(char *)srct->filename);
      }
      break;
    case TYPE_SDIR:
      if(dirnum == DIRMAX-1)
        break;
      dirnum++;   
      if (srct->filename[0] == '/') {
          strcpy((char *)dirs[dirnum], (char *)srct->filename);
      } else {
          sprintf((char *)dirs[dirnum], "%s%s%s", (char *)cdir, (strcmp((char*)cdir, "/") == 0)?"":"/",(char *)srct->filename);
      }
      strcpy((char *)cdir, (char *)dirs[dirnum]);
      sel = disp = 0;
      ret = false;
      break;
    case TYPE_UDIR:
      dirnum--;   
      strcpy((char *)cdir, (char *)dirs[dirnum]);
//      printf("chk spri4: %s, %d, %d\n", (char *)dirs[dirnum], strlen((char *)dirs[dirnum]), dirnum);
      sel = disp = 0;
      ret = false;
      break;
  }
//  printf("ck5 %s %s %d\n", cdir, dst, ret);
  return ret;
}

void filerinit(){
  sel=0;
  disp=0;
  dirnum=0;
  playall=false;
  snprintf((char*)cdir,PATHMAX,"/");
  snprintf((char*)dirs[0],PATHMAX,"/");
}
#endif // ARDUINO_M5STACK_Cardputer
