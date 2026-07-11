#pragma once

#if defined(IS_CARDPUTER)

#define VERSION 0.9

#define STATNUM 4
#define STATMAX 10
#define STATCLR 0
#define STATLOAD 1
#define STATLOOP 2
#define STATALL 3

#define DISPMAX 6
#define LISTMAX 256   // max files in a directory
#define PATHMAX 256   // filename max include pathname

#define TYPE_VGM  2
#define TYPE_MDX  3
#define TYPE_SDIR  1
#define TYPE_UDIR  0

#define DIRMAX 10   // directory depth max

struct fl{
  uint8_t filename[PATHMAX];
  uint8_t type;
};

void disptitle(int stat, char *title);
void dispmenu();
void hitkey();
int makevgmlist(fs::FS &fs);
void dispfiles(int disp, int sel, bool start);
void deletetemp(fs::FS &fs);
int selectfile();
bool cnvfile(fs::FS &fs, struct fl *srct, uint8_t *dst);
void filerinit();
extern struct fl *filelist;
extern int filenum;

#endif // IS_CARDPUTER
