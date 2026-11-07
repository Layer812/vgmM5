#if defined(IS_CARDPUTER)
#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
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

void dispmenu(){
  auto& lcd=M5Cardputer.Display;
  lcd.fillRect(0,36,240,99,BLACK);
  lcd.setTextSize(1.5f);
  lcd.setTextColor(CYAN);
  lcd.drawString("CARDPUTER R67 / SD",7,39);
  lcd.setTextColor(WHITE);
  lcd.drawString(";/. Move  ,// Page",7,58);
  lcd.drawString("SPACE Play  A All",7,77);
  lcd.drawString("+/- Volume  B Back",7,96);
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

void dispfiles(int disp, int sel, bool start) {
  auto& lcd = M5Cardputer.Display;
  lcd.fillRect(0,35,240,81,BLACK);
  lcd.setTextSize(1.5f);
  lcd.setTextWrap(false);
  for (int i=0;i<DISPMAX && i+disp<filenum;i++) {
    const int idx=i+disp;
    if(idx<0||!filelist)break;
    const int y=38+i*19;
    const bool chosen=(idx==sel);
    if(chosen)lcd.fillRect(3,y,234,19,start?0x5BC0:0x03A0);
    const bool is_dir=filelist[idx].type==TYPE_SDIR||filelist[idx].type==TYPE_UDIR;
    lcd.setTextColor(chosen?BLACK:(is_dir?CYAN:WHITE));
    const char* label=(const char*)filelist[idx].filename;
    const char* basename=strrchr(label,'/');
    if(basename&&basename[1])label=basename+1;
    String line=is_dir?"[DIR] ":"";
    line+=label;
    while(line.length() && lcd.textWidth(line+"..")>225) {
      int cut=(int)line.length()-1;
      while(cut>0 && (((uint8_t)line[(unsigned)cut]&0xC0u)==0x80u))--cut;
      line.remove((unsigned)cut);
    }
    lcd.drawString(line,7,y+1);
  }
  lcd.fillRect(0,116,240,19,BLACK);
  lcd.setTextColor(LIGHTGREY);
  lcd.drawString(";/. Move  ,// Page A All",5,118);
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
