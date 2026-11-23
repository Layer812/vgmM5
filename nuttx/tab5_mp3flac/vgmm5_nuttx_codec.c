// NuttX POSIX PCM codec. No Arduino, ESP-IDF, ES8388 or I2S dependency.
// Vendor imports pinned in tab5/firmware/src/vendor/ (see README).
// License: original glue MIT-0; minimp3 CC0, dr_flac public-domain/MIT-0.
#include "vgmm5_nuttx_codec.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "vendor/minimp3.h"
#define DR_FLAC_NO_STDIO
#define DR_FLAC_IMPLEMENTATION
#include "vendor/dr_flac.h"

enum { CHUNK=512, MP3_IN=8192, FLAC_READ=1024 };
typedef struct {
  int fd;
  uint8_t mp3_mode;
  uint8_t exhausted;
  uint8_t bad;
  unsigned src_channels;
  uint32_t src_rate;
  mp3dec_t mp3;
  uint8_t mp3_buf[MP3_IN];
  size_t mp3_len;
  int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  size_t pcm_frames, pcm_index;
  drflac *flac;
} stream_t;

static size_t io_read(void *opaque, void *buf, size_t n) {
  stream_t *s=(stream_t*)opaque;
  size_t total=0;
  while(total<n) {
    const ssize_t count=read(s->fd, (char*)buf+total, n-total);
    if(count>0) {total+=(size_t)count;continue;}
    if(count<0 && errno==EINTR) continue;
    break;
  }
  return total;
}
static drflac_bool32 io_seek(void *opaque,int offset,drflac_seek_origin origin) {
  stream_t *s=(stream_t*)opaque;
  int whence;
  switch(origin) {
    case DRFLAC_SEEK_SET: whence=SEEK_SET; break;
    case DRFLAC_SEEK_CUR: whence=SEEK_CUR; break;
    case DRFLAC_SEEK_END: whence=SEEK_END; break;
    default:return DRFLAC_FALSE;
  }
  return lseek(s->fd,(off_t)offset,whence)>=0?DRFLAC_TRUE:DRFLAC_FALSE;
}
static drflac_bool32 io_tell(void *opaque,drflac_int64 *cursor) {
  stream_t *s=(stream_t*)opaque;
  const off_t pos=lseek(s->fd,0,SEEK_CUR);
  if(pos<0 || !cursor)return DRFLAC_FALSE;
  *cursor=(drflac_int64)pos;
  return DRFLAC_TRUE;
}
static int stopping(vgmm5_stop_requested cb,void *opaque) {
  return cb?cb(opaque):0;
}
static void discard_mp3(stream_t *s,size_t count) {
  if(count>s->mp3_len)count=s->mp3_len;
  s->mp3_len-=count;
  if(s->mp3_len)memmove(s->mp3_buf,s->mp3_buf+count,s->mp3_len);
}
static int refill_mp3(stream_t *s) {
  if(s->exhausted || s->mp3_len>=sizeof(s->mp3_buf))return 0;
  ssize_t r=read(s->fd,s->mp3_buf+s->mp3_len,sizeof(s->mp3_buf)-s->mp3_len);
  if(r<0 && errno==EINTR)return refill_mp3(s);
  if(r<0) {s->bad=1;return 0;}
  if(r==0) {s->exhausted=1;return 0;}
  s->mp3_len+=(size_t)r;
  return 1;
}
static int next_mp3_block(stream_t *s) {
  // Bound damaged-file resynchronization, avoid unbounded zero-progress loops.
  for(unsigned tries=0;tries<16384;tries++) {
    if(s->mp3_len<4096 && !s->exhausted)refill_mp3(s);
    if(s->mp3_len<4)return 0;
    mp3dec_frame_info_t info={0};
    const int n=mp3dec_decode_frame(&s->mp3,s->mp3_buf,(int)s->mp3_len,
                                     s->pcm,&info);
    if(info.frame_bytes>0 && (size_t)info.frame_bytes<=s->mp3_len) {
      discard_mp3(s,(size_t)info.frame_bytes);
    } else {
      if(!s->exhausted && s->mp3_len<sizeof(s->mp3_buf)) {
        refill_mp3(s);
        continue;
      }
      discard_mp3(s,1);
    }
    if(n<=0)continue;
    if(info.channels<1 || info.channels>2 ||
       info.hz<8000 || info.hz>48000 || n>1152) {
      s->bad=1;return 0;
    }
    if(s->src_rate && (s->src_rate!=(uint32_t)info.hz ||
                       s->src_channels!=(unsigned)info.channels)) {
      s->bad=1;return 0;  // reject mid-song format switching
    }
    s->src_rate=(uint32_t)info.hz;
    s->src_channels=(unsigned)info.channels;
    s->pcm_frames=(size_t)n;
    s->pcm_index=0;
    return 1;
  }
  s->bad=1;
  return 0;
}
static int next_pcm_sample(stream_t *s,int16_t *l,int16_t *r) {
  if(s->pcm_index>=s->pcm_frames) {
    if(s->mp3_mode) {
      if(!next_mp3_block(s))return 0;
    } else {
      const drflac_uint64 n=drflac_read_pcm_frames_s16(
          s->flac,FLAC_READ,s->pcm);
      if(!n)return 0;
      s->pcm_frames=(size_t)n;
      s->pcm_index=0;
    }
  }
  const size_t i=s->pcm_index++;
  if(s->src_channels==1) {
    *l=*r=s->pcm[i];
  } else {
    *l=s->pcm[i*2];*r=s->pcm[i*2+1];
  }
  return 1;
}
static int mp3_skip_id3(stream_t *s) {
  uint8_t hdr[10];
  const ssize_t n=read(s->fd,hdr,sizeof(hdr));
  if(n!=10 || memcmp(hdr,"ID3",3)!=0)
    return lseek(s->fd,0,SEEK_SET)>=0?0:-1;
  for(int i=6;i<10;i++)if(hdr[i]&0x80)return -1;
  const uint32_t size=((uint32_t)hdr[6]<<21)|((uint32_t)hdr[7]<<14)|
                       ((uint32_t)hdr[8]<<7)|hdr[9];
  const uint32_t extra=(hdr[5]&0x10)?10:0;
  if(size>(16u<<20))return -1;
  return lseek(s->fd,10+(off_t)size+extra,SEEK_SET)>=0?0:-1;
}
static int path_flac(const char* path) {
  if(!path)return -1;
  const char* ext=strrchr(path,'.');
  if(!ext)return -1;
  // Case-insensitive without nonportable strcasecmp
  const char* mp3=".mp3";const char* flac=".flac";
  char lower[7]={0};
  size_t i=0;
  while(ext[i] && i<6) {
    char c=ext[i];
    lower[i]=(c>='A'&&c<='Z')?(char)(c+32):c;
    i++;
  }
  if(ext[i])return -1;
  if(strcmp(lower,mp3)==0)return 0;
  if(strcmp(lower,flac)==0)return 1;
  return -1;
}
static int16_t interp_s16(int16_t a,int16_t b,uint32_t phase,uint32_t rate) {
  const int64_t value=(int64_t)a*(int64_t)(rate-phase)
                      +(int64_t)b*(int64_t)phase;
  return (int16_t)(value/(int64_t)rate);
}
int vgmm5_decode_file(const char *path, uint32_t output_hz,
                      vgmm5_pcm_sink sink, vgmm5_stop_requested stop,
                      void *opaque, vgmm5_decode_stats *stats) {
  const int format=path_flac(path);
  if(format<0 || !sink || output_hz<8000 || output_hz>96000)
    return VGMM5_CODEC_UNSUPPORTED;
  if(stats)memset(stats,0,sizeof(*stats));
  if(stopping(stop,opaque))return VGMM5_CODEC_STOPPED;
  stream_t* s=(stream_t*)calloc(1,sizeof(*s));
  if(!s)return VGMM5_CODEC_NO_MEMORY;
  s->fd=-1;
  s->mp3_mode=(format==0);
  int rc=VGMM5_CODEC_OK;
  s->fd=open(path,O_RDONLY|O_CLOEXEC);
  if(s->fd<0) {rc=VGMM5_CODEC_BAD_FILE;goto done;}
  if(s->mp3_mode) {
    mp3dec_init(&s->mp3);
    if(mp3_skip_id3(s)!=0 || !next_mp3_block(s)) {
      rc=VGMM5_CODEC_UNSUPPORTED;goto done;
    }
  } else {
    s->flac=drflac_open(io_read,io_seek,io_tell,s,NULL);
    if(!s->flac) {rc=VGMM5_CODEC_UNSUPPORTED;goto done;}
    s->src_rate=s->flac->sampleRate;
    s->src_channels=s->flac->channels;
    if(s->src_rate<8000 || s->src_rate>96000 ||
       (s->src_channels!=1 && s->src_channels!=2)) {
      rc=VGMM5_CODEC_UNSUPPORTED;goto done;
    }
  }
  if(stats) {
    stats->source_hz=s->src_rate;
    stats->output_hz=output_hz;
    stats->channels=(uint8_t)s->src_channels;
    stats->flac=(uint8_t)format;
  }
  int16_t la=0,ra=0,lb=0,rb=0;
  if(!next_pcm_sample(s,&la,&ra)) {rc=VGMM5_CODEC_UNSUPPORTED;goto done;}
  int last=0;
  if(!next_pcm_sample(s,&lb,&rb)){lb=la;rb=ra;last=1;}
  uint32_t phase=0;
  int ended=0;
  int16_t output[CHUNK*2];
  while(!ended) {
    if(stopping(stop,opaque)){rc=VGMM5_CODEC_STOPPED;break;}
    size_t used=0;
    while(used<CHUNK&&!ended) {
      output[used*2]=interp_s16(la,lb,phase,output_hz);
      output[used*2+1]=interp_s16(ra,rb,phase,output_hz);
      used++;
      phase+=s->src_rate;
      while(phase>=output_hz) {
        phase-=output_hz;
        if(last) {ended=1;break;}
        la=lb;ra=rb;
        if(!next_pcm_sample(s,&lb,&rb)) {
          lb=la;rb=ra;last=1;
        }
      }
    }
    if(used) {
      // The sink is the ONLY owner of the output block during this call.
      // It must copy or wait for DMA completion before returning.
      if(sink(opaque,output,used,output_hz)!=0) {
        rc=VGMM5_CODEC_IO;break;
      }
      if(stats)stats->frames_output+=(uint64_t)used;
    }
  }
  if(s->bad && rc==VGMM5_CODEC_OK)rc=VGMM5_CODEC_UNSUPPORTED;
done:
  if(s->flac)drflac_close(s->flac);
  if(s->fd>=0)close(s->fd);
  free(s);
  return rc;
}
