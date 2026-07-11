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
  M5Cardputer.Lcd.fillRect(20,36,200,100,BLUE);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 38);
  M5.Lcd.println("   m: menu");
  M5.Lcd.println("   a: play all");
  M5.Lcd.println("   +/-: maser vol");
  M5.Lcd.println("   9/0: pcm vol");
  M5.Lcd.println("   \" \": play/stop");
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
  for(i = 0; i < 4; i++)
    exttmp[i] = tolower(ext[i]);

  return (strcmp(".vgm", exttmp) == 0 || strcmp(".vgz", exttmp) == 0);
}

bool ismdxfile(const char *fileName) {
  int i; char exttmp[5];
  exttmp[4] = 0;
  const char *ext = strrchr(fileName, '.');
  if(ext == NULL)
    return false;
  for(i = 0; i < 4; i++)
    exttmp[i] = tolower(ext[i]);
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
      memcpy(filelist[i].filename, file.name(), PATHMAX);
      filelist[i].type = TYPE_VGM;
      i++;
    } else if (ismdxfile((const char *)file.name())){
      memcpy(filelist[i].filename, file.name(), PATHMAX);
      filelist[i].type = TYPE_MDX;
      i++;
    } else if(file.isDirectory()){
      memcpy(filelist[i].filename, file.name(), PATHMAX);
      filelist[i].type = TYPE_SDIR;
      i++;
    }
//    printf("ck5 %d %s\n", i, filelist[i]);
    file = root.openNextFile();
//    printf("ck6\n");
  }
  return i;
}

void dispfiles(int disp, int sel, bool start){
  int i;
  menuflag = false;
  M5Cardputer.Display.fillRect(0,36, 240,135, BLACK);
  M5Cardputer.Display.setCursor(0, 37);

//  M5Cardputer.Display.fillScreen(BLACK);
  for (i = 0; i < DISPMAX; i++){
    if(filelist[i + disp].type == TYPE_SDIR || filelist[i + disp].type == TYPE_UDIR)
      M5Cardputer.Display.setTextColor(LIGHTGREY);
    if((i + disp)== sel)
      M5Cardputer.Display.setTextColor(BLACK, start? YELLOW: GREEN);

    const char* fname = (const char*)filelist[i + disp].filename;
    const char* basename = strrchr(fname, '/');
    if (basename) fname = basename + 1;
    if (fname[0] == '\0') fname = (const char*)filelist[i + disp].filename;

    M5Cardputer.Display.printf("%.*s\n", 20, fname);
    M5Cardputer.Display.setTextColor(WHITE);
  }
}


int selectfile(){
    dispfiles(disp, sel, false);
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
      M5Cardputer.update();
      if (M5Cardputer.Keyboard.isChange()) {
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
          if (M5Cardputer.Keyboard.isKeyPressed(' ')){
            dispfiles(disp, sel, true);
            return sel;
          }
          if (M5Cardputer.Keyboard.isKeyPressed('') || M5Cardputer.Keyboard.isKeyPressed('`')){
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

}
#endif // ARDUINO_M5STACK_Cardputer
