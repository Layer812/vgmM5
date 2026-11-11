#if defined(IS_CARDPUTER)
#include <SD.h> // Must precede M5GFX/M5Cardputer for drawJpgFile(fs::FS,...)
#include <M5Cardputer.h>
#include <SPI.h>
#include <time.h>
// #include <ESP32-targz.h>
#include "filer.hpp"
#include "cardputer_artwork.hpp"
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

// Directory entries and file sizes are enumerated only when the folder changes
// or the user explicitly requests B/rescan. Moving the cursor NEVER relists SD.
static bool file_index_valid=false;
static char indexed_directory[PATHMAX]={};
static void invalidate_file_index(){file_index_valid=false;}

// Reuse a displayed album cover across adjacent files in the same folder.
// A path hash + kind is enough: no persistent 64x64 bitmap in RAM.
static bool browser_preview_valid=false;
static uint32_t browser_preview_hash=0;
static uint8_t browser_preview_kind=0;

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
  if(file_index_valid && filelist &&
     strcmp(indexed_directory,(const char*)cdir)==0) return filenum;
  file_index_valid=false;
  int i = 0;
  if (filelist == nullptr) {
      filelist = (struct fl *)malloc(sizeof(struct fl) * LISTMAX);
  }
  if (filelist == nullptr) return 0;
 // printf("ck1\n");
   //printf("ck6 %s\n", cdir);
  File root = fs.open((const char *)cdir);
  if (!root || !root.isDirectory()) {
    if(root) root.close();
    filenum=0;
    return 0;
  }
  //printf("ck7 %d\n", dirnum);
  memset(filelist, 0, sizeof(struct fl) * LISTMAX);
  // One artwork hint from the existing directory enumeration (not a new scan).
  char album_image_hint[PATHMAX]={};
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
      filelist[i].size_bytes = (uint32_t)file.size();
      filelist[i].type = TYPE_VGM;
      i++;
    } else if (ismdxfile((const char *)file.name())){
      snprintf((char*)filelist[i].filename, PATHMAX, "%s", file.name());
      filelist[i].size_bytes = (uint32_t)file.size();
      filelist[i].type = TYPE_MDX;
      i++;
    } else if(file.isDirectory()){
      const char* fname = file.name();
      if (strcmp(fname, "System Volume Information") != 0 && strcmp(fname, "/System Volume Information") != 0 &&
          strcmp(fname, ".VGMM5") != 0 && strcmp(fname, "/.VGMM5") != 0 &&
          strcmp(fname, ".vgmm5") != 0 && strcmp(fname, "/.vgmm5") != 0 &&
          strcmp(fname, "VGMM5") != 0 && strcmp(fname, "/VGMM5") != 0 &&
          strcmp(fname, "vgmm5") != 0 && strcmp(fname, "/vgmm5") != 0) {
        snprintf((char*)filelist[i].filename, PATHMAX, "%s", fname);
        filelist[i].type = TYPE_SDIR;
        i++;
      }
    }
//    printf("ck5 %d %s\n", i, filelist[i]);
    if(!album_image_hint[0] && !file.isDirectory()) {
      const char* basename=file.name();
      const char* slash=basename?strrchr(basename,'/'):nullptr;
      if(slash)basename=slash+1;
      String lower=basename?String(basename):String("");
      lower.toLowerCase();
      if(lower.endsWith(".png") || lower.endsWith(".jpg") ||
         lower.endsWith(".jpeg"))
        snprintf(album_image_hint,sizeof(album_image_hint),"%s",basename);
    }
    file = root.openNextFile();
//    printf("ck6\n");
  }
  if(file)file.close();
  root.close();
  // Ordinary PNG/JPEG artwork is resized only when entering this folder.
  // The existing single SD listing supplies a fallback image filename.
  cardputer_artwork::prepare_album_once((const char*)cdir,album_image_hint);
  filenum=i;
  snprintf(indexed_directory,sizeof(indexed_directory),"%s",(const char*)cdir);
  file_index_valid=true;
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

static void browser_draw_info(int selection) {
  auto& lcd=M5Cardputer.Display;
  if(selection<0||selection>=filenum||!filelist)return;
  const struct fl& entry=filelist[selection];
  const bool directory=entry.type==TYPE_SDIR||entry.type==TYPE_UDIR;
  const char* stored=(const char*)entry.filename;
  const char* slash=strrchr(stored,'/');
  const char* name=slash&&slash[1]?slash+1:stored;
  char path[PATHMAX+16]={};
  if(!browser_make_path(path,sizeof(path),stored))return;
  const cardputer_artwork::Kind kind =
      entry.type==TYPE_UDIR?cardputer_artwork::PARENT:
      entry.type==TYPE_SDIR?cardputer_artwork::FOLDER:
      entry.type==TYPE_MDX?cardputer_artwork::MDX:cardputer_artwork::VGM;

  const uint32_t album_key=cardputer_artwork::album_key(path,kind);
  // Never clear/reopen/re-read the same album image while moving between
  // tracks in that folder. Only redraw the two metadata lines (38px).
  const bool new_album=!browser_preview_valid ||
                       browser_preview_hash!=album_key ||
                       browser_preview_kind!=(uint8_t)kind;
  if(new_album) {
    lcd.fillRect(144,27,96,108,BLACK);
    lcd.drawFastVLine(143,27,108,0x2C68);
    // Exactly one deterministic /.VGMM5/XXXXXXXX.RGB open; no PNG lookup.
    cardputer_artwork::draw(path,kind,160,31,64);
    browser_preview_hash=album_key;
    browser_preview_kind=(uint8_t)kind;
    browser_preview_valid=true;
  }
  lcd.fillRect(145,97,95,38,BLACK);
  const char* format=directory?"DIR":entry.type==TYPE_MDX?"MDX":"VGM";
  const char* extension=strrchr(name,'.');
  if(!directory && extension &&
     (strcmp(extension,".vgz")==0||strcmp(extension,".VGZ")==0))format="VGZ";

  // Size comes from the single makevgmlist() enumeration when we entered
  // this folder. No SD.open(file), file header read or folder scan on
  // up/down cursor events, even with hundreds of tracks.
  char sizeinfo[16]={};
  if(!directory) {
    const uint32_t length=entry.size_bytes;
    if(length>=1048576u)snprintf(sizeinfo,sizeof(sizeinfo),"%lu.%luM",
      (unsigned long)(length/1048576u),
      (unsigned long)((length%1048576u)*10u/1048576u));
    else snprintf(sizeinfo,sizeof(sizeinfo),"%luK",
                  (unsigned long)((length+1023u)/1024u));
  }
  lcd.setTextSize(1.5f);
  lcd.setTextColor(directory?CYAN:0x9FE7);
  String line=String(format)+(sizeinfo[0]?String(" ")+sizeinfo:"");
  while(line.length()&&lcd.textWidth(line)>87)line.remove(line.length()-1);
  lcd.drawString(line,148,99);

  String label=directory?(entry.type==TYPE_UDIR?"PARENT /":"OPEN /"):
               entry.type==TYPE_MDX?"X68000 MDX":
               (strcmp(format,"VGZ")==0?"COMPRESSED":"VGM AUDIO");
  lcd.setTextColor(WHITE);
  while(label.length()&&lcd.textWidth(label)>87)label.remove(label.length()-1);
  lcd.drawString(label,148,117);
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
    // Cyan folder names end with a slash to keep the list compact.
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

int selectfile() {
  browser_help_visible=false;
  browser_preview_valid=false; // After playback/help the LCD cover was cleared.
  if(filenum<0)filenum=0;
  if(filenum>0&&sel>=filenum)sel=filenum-1;
  if(sel<0)sel=0;
  if(disp>sel)disp=sel;
  if(filenum>0) {
    disp=(sel/DISPMAX)*DISPMAX;
    dispfiles(disp,sel,false);
  } else {
    auto& lcd=M5Cardputer.Display;
    lcd.fillRect(0,27,240,108,BLACK);
    lcd.setTextSize(1.5f);
    lcd.setTextColor(YELLOW);
    lcd.drawString("NO MUSIC FILES",7,45);
    lcd.setTextColor(WHITE);
    lcd.drawString("Put VGM/VGZ/MDX",7,67);
    lcd.drawString("on microSD",7,87);
    lcd.drawString("B: RESCAN",7,107);
  }

  // Folder-local auto-play skips directories and wraps to the first track.
  if(playall&&filenum>0) {
    int next=sel;
    for(int attempt=0;attempt<filenum;++attempt) {
      next=(next+1)%filenum;
      if(filelist[next].type==TYPE_VGM||filelist[next].type==TYPE_MDX) {
        sel=next;
        disp=(sel/DISPMAX)*DISPMAX;
        dispfiles(disp,sel,true);
        return sel;
      }
    }
    playall=false;
  }

  while(1) {
    delay(10); // Keyboard polling must yield to the sound and DMA tasks.
    M5Cardputer.update();
    if(!M5Cardputer.Keyboard.isChange())continue;
    const auto state=M5Cardputer.Keyboard.keysState();

    if(M5Cardputer.Keyboard.isKeyPressed('m')) {
      if(browser_help_visible) {
        browser_help_visible=false;
        browser_preview_valid=false;
        disptitle(STATCLR,(char*)"");
        if(filenum>0)dispfiles(disp,sel,false);
        else return -1;
      } else {
        dispmenu();
      }
      continue;
    }
    if(browser_help_visible) {
      browser_help_visible=false;
      browser_preview_valid=false;
      disptitle(STATCLR,(char*)"");
      if(filenum>0)dispfiles(disp,sel,false);
      else return -1;
      continue;
    }
    if(filenum==0) {
      if(M5Cardputer.Keyboard.isKeyPressed('b')||
         M5Cardputer.Keyboard.isKeyPressed(0x60)||
         M5Cardputer.Keyboard.isKeyPressed('\b')) {
        invalidate_file_index();
        return -1;
      }
      continue;
    }

    bool moved=false;
    if(M5Cardputer.Keyboard.isKeyPressed(';')&&sel>0) {
      --sel; moved=true;
    } else if(M5Cardputer.Keyboard.isKeyPressed('.')&&sel+1<filenum) {
      ++sel; moved=true;
    } else if(M5Cardputer.Keyboard.isKeyPressed(',')&&sel>0) {
      sel=sel>=DISPMAX?sel-DISPMAX:0; moved=true;
    } else if(M5Cardputer.Keyboard.isKeyPressed('/')&&sel<filenum-1) {
      sel=sel+DISPMAX<filenum?sel+DISPMAX:filenum-1; moved=true;
    }
    if(moved) {
      disp=(sel/DISPMAX)*DISPMAX;
      dispfiles(disp,sel,false);
      continue;
    }
    if(M5Cardputer.Keyboard.isKeyPressed(' ')||state.enter) {
      playall=false;
      dispfiles(disp,sel,true);
      return sel;
    }
    if(M5Cardputer.Keyboard.isKeyPressed('a')&&
       (filelist[sel].type==TYPE_VGM||filelist[sel].type==TYPE_MDX)) {
      playall=true;
      loopflag=false;
      dispfiles(disp,sel,true);
      return sel;
    }
    if(M5Cardputer.Keyboard.isKeyPressed('b')||
       M5Cardputer.Keyboard.isKeyPressed(0x60)||
       M5Cardputer.Keyboard.isKeyPressed('\b')) {
      invalidate_file_index();
      return -1;
    }
    if(M5Cardputer.Keyboard.isKeyPressed('=')) {
      int value=(int)M5.Speaker.getVolume()+20;
      M5.Speaker.setVolume(value>255?255:value);
    } else if(M5Cardputer.Keyboard.isKeyPressed('-')) {
      int value=(int)M5.Speaker.getVolume()-20;
      M5.Speaker.setVolume(value<0?0:value);
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
  invalidate_file_index();
  browser_preview_valid=false;
  sel=0;
  disp=0;
  dirnum=0;
  playall=false;
  snprintf((char*)cdir,PATHMAX,"/");
  snprintf((char*)dirs[0],PATHMAX,"/");
}
#endif // ARDUINO_M5STACK_Cardputer
