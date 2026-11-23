// Tab5 R68: SD MP3/FLAC decoder. Uses the ORIGINAL R67 512-frame,
// release-callback-protected stereo PCM ring and the existing ES8388/I2S.
// MP3: lieff/minimp3 (CC0), FLAC: mackron/dr_flac (public domain/MIT-0).
// Files are streamed. No full-track allocation, no speaker reinitialization.
#include <Arduino.h>
#include <SD.h>
#include <M5Unified.h>
#include <atomic>
#include <new>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdint.h>
#include "vgm_engine.h"
#include "mdx_engine.hpp"
#include "tab5_pcm_file_engine.hpp"
#include "tab5_spectrum.hpp"
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "vendor/minimp3.h"
#define DR_FLAC_NO_STDIO
#define DR_FLAC_IMPLEMENTATION
#include "vendor/dr_flac.h"

namespace {
constexpr unsigned kTargetRate=44100;
constexpr unsigned kMp3Input=8192;
constexpr unsigned kFlacFrames=1024;
std::atomic<bool> s_stop{false};
std::atomic<bool> s_active{false};
std::atomic<bool> s_worker{false};
std::atomic<bool> s_owned{false};
std::atomic<bool> s_open_done{false};
std::atomic<bool> s_open_ok{false};
std::atomic<unsigned> s_source_rate{0};
std::atomic<unsigned> s_source_channels{0};
char s_path[256]={};
char s_error[112]="";
void error(const char* why) {
  snprintf(s_error,sizeof(s_error),"%s",why?why:"MP3/FLAC decode failed");
}
bool is_mp3(const char* p) {
  const char* dot=strrchr(p,'.');
  return dot&&strcasecmp(dot,".mp3")==0;
}
bool is_flac(const char* p) {
  const char* dot=strrchr(p,'.');
  return dot&&strcasecmp(dot,".flac")==0;
}
struct Decoder {
  File file;
  drflac* flac=nullptr;
  mp3dec_t mp3{};
  uint8_t bytes[kMp3Input]{};
  size_t filled=0;
  int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
  size_t frames=0;
  size_t cursor=0;
  uint32_t rate=0;
  unsigned channels=0;
  bool mp3_mode=false;
  bool corrupt=false;

  ~Decoder() {
    if(flac) drflac_close(flac);
    if(file) file.close();
  }
  static size_t flac_read(void* user,void* out,size_t len) {
    auto* self=static_cast<Decoder*>(user);
    if(!self||!self->file||s_stop.load(std::memory_order_relaxed))return 0;
    // sd::File.read() returns an int; clamp to the open file's remaining bytes.
    const int n=self->file.read(static_cast<uint8_t*>(out),len);
    return n>0?(size_t)n:0;
  }
  static drflac_bool32 flac_seek(void* user,int offset,drflac_seek_origin origin) {
    auto* self=static_cast<Decoder*>(user);
    if(!self||!self->file)return DRFLAC_FALSE;
    int64_t base=0;
    switch(origin) {
      case DRFLAC_SEEK_SET: base=0; break;
      case DRFLAC_SEEK_CUR: base=self->file.position(); break;
      case DRFLAC_SEEK_END: base=self->file.size(); break;
      default:return DRFLAC_FALSE;
    }
    const int64_t target=base+(int64_t)offset;
    if(target<0||(uint64_t)target>(uint64_t)self->file.size())return DRFLAC_FALSE;
    return self->file.seek((uint32_t)target)?DRFLAC_TRUE:DRFLAC_FALSE;
  }
  static drflac_bool32 flac_tell(void* user,drflac_int64* pos) {
    auto* self=static_cast<Decoder*>(user);
    if(!self||!self->file||!pos)return DRFLAC_FALSE;
    *pos=(drflac_int64)self->file.position();
    return DRFLAC_TRUE;
  }
  bool refill() {
    if(filled>=sizeof(bytes)||!file||!file.available())return false;
    int got=file.read(bytes+filled,sizeof(bytes)-filled);
    if(got<=0)return false;
    filled+=(size_t)got;
    return true;
  }
  bool decode_mp3_frame() {
    // Decode a bounded number of frames per call: corrupted files cannot
    // spin forever on the audio core or overflow an output sample buffer.
    for(unsigned tries=0;tries<512 && !s_stop.load();++tries) {
      if(filled<4096)refill();
      if(!filled)return false;
      mp3dec_frame_info_t info{};
      const int n=mp3dec_decode_frame(&mp3,bytes,(int)filled,pcm,&info);
      if(info.frame_bytes>0 && (size_t)info.frame_bytes<=filled) {
        const size_t used=(size_t)info.frame_bytes;
        filled-=used;
        if(filled)memmove(bytes,bytes+used,filled);
      } else if(!file.available()) {
        // Attempt a final resynchronization, then terminate at EOF.
        if(filled<4)return false;
        --filled;
        memmove(bytes,bytes+1,filled);
      } else {
        if(filled<sizeof(bytes)) {refill();continue;}
        --filled;
        memmove(bytes,bytes+1,filled);
      }
      if(n<=0)continue;
      if(info.channels<1||info.channels>2||info.hz<8000||info.hz>48000) {
        corrupt=true;return false;
      }
      if(rate && (rate!=(uint32_t)info.hz||channels!=(unsigned)info.channels)) {
        corrupt=true;return false; // unsupported mid-track format change
      }
      rate=(uint32_t)info.hz;
      channels=(unsigned)info.channels;
      frames=(size_t)n;
      cursor=0;
      return true;
    }
    corrupt=true;
    return false;
  }
  bool begin(const char* path) {
    mp3_mode=is_mp3(path);
    file=SD.open(path,FILE_READ);
    if(!file||file.isDirectory()) {error("Cannot open MP3/FLAC on SD");return false;}
    if(mp3_mode) {
      // Skip common ID3v2 header without loading a tag or entire MP3 into RAM.
      uint8_t header[10]{};
      if(file.read(header,10)==10 && !memcmp(header,"ID3",3)) {
        const uint32_t len=((header[6]&127u)<<21)|((header[7]&127u)<<14)|
                           ((header[8]&127u)<<7)|(header[9]&127u);
        const uint32_t offset=10u+len+((header[5]&0x10u)?10u:0u);
        if(offset>=file.size()||!file.seek(offset)) {error("Invalid ID3 header");return false;}
      } else {
        file.seek(0);
      }
      mp3dec_init(&mp3);
      if(!decode_mp3_frame()) {error("MP3 frame/header unsupported");return false;}
    } else {
      flac=drflac_open(flac_read,flac_seek,flac_tell,this,nullptr);
      if(!flac) {error("Invalid FLAC stream");return false;}
      rate=flac->sampleRate;
      channels=flac->channels;
      if(channels<1||channels>2||rate<8000||rate>48000) {
        error("Only mono/stereo 8-48 kHz FLAC");return false;
      }
    }
    return true;
  }
  bool next(int16_t& l,int16_t& r) {
    if(cursor>=frames) {
      if(mp3_mode) {
        if(!decode_mp3_frame())return false;
      } else {
        const drflac_uint64 count=drflac_read_pcm_frames_s16(flac,kFlacFrames,pcm);
        if(!count)return false;
        frames=(size_t)count;
        cursor=0;
      }
    }
    if(channels==1) {
      l=r=pcm[cursor];
    } else {
      l=pcm[cursor*2];
      r=pcm[cursor*2+1];
    }
    ++cursor;
    return true;
  }
};
static inline int16_t lerp(int16_t a,int16_t b,uint32_t frac) {
  const int64_t mixed=(int64_t)a*(kTargetRate-frac)+(int64_t)b*frac;
  return (int16_t)(mixed/(int64_t)kTargetRate);
}
void task(void*) {
  auto* dec=new(std::nothrow) Decoder();
  if(!dec) {
    error("Decoder allocation failed");
    s_open_ok.store(false,std::memory_order_release);
    s_open_done.store(true,std::memory_order_release);
    s_active.store(false,std::memory_order_release);
    s_worker.store(false,std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }
  bool ok=dec->begin(s_path);
  int16_t la=0,ra=0,lb=0,rb=0;
  if(ok && !dec->next(la,ra)) {
    error("Empty MP3/FLAC audio stream");ok=false;
  }
  if(ok && !dec->next(lb,rb)) {lb=la;rb=ra;}
  if(ok) {
    s_source_rate.store(dec->rate,std::memory_order_release);
    s_source_channels.store(dec->channels,std::memory_order_release);
  }
  s_open_ok.store(ok,std::memory_order_release);
  s_open_done.store(true,std::memory_order_release);
  bool end_after_current=false;
  bool finished=false;
  uint32_t phase=0;
  if(ok) {
    // Prime the R67 shared speaker ring from the dedicated producer (Core 0).
    // Exactly one of VGM, MDX and compressed-audio engines may publish.
    while(!s_stop.load(std::memory_order_acquire) && !finished) {
      if(wav_count.load(std::memory_order_acquire)>=(VGM_WAV_BUFF_COUNT-4)) {
        vTaskDelay(1);
        continue;
      }
      int16_t* out=wav_buff[wd];
      size_t n=0;
      while(n<VGM_BUFF_SIZE && !s_stop.load(std::memory_order_relaxed)) {
        out[n*2]=lerp(la,lb,phase);
        out[n*2+1]=lerp(ra,rb,phase);
        ++n;
        phase+=dec->rate;
        while(phase>=kTargetRate) {
          phase-=kTargetRate;
          if(end_after_current) {finished=true;break;}
          la=lb;ra=rb;
          if(!dec->next(lb,rb)) {
            if(dec->corrupt)error("MP3 frame stream changed or corrupted");
            lb=la;rb=ra;
            end_after_current=true;
          }
        }
        if(finished)break;
      }
      if(n) {
        wav_buff_size[wd]=n;
        tab5::g_spectrum_tap.publish_if_due(wav_buff[wd],n);
        wd=(wd+1)%VGM_WAV_BUFF_COUNT;
        vgm_audio_queue_publish();
      }
    }
    if(!s_stop.load(std::memory_order_acquire))
      vgm_audio_queue_begin_tail_drain();
  }
  delete dec;
  s_active.store(false,std::memory_order_release);
  s_worker.store(false,std::memory_order_release);
  vTaskDelete(nullptr);
}
} // namespace

bool pcm_file_engine_play(const char* path,bool use_sd) {
  if(!use_sd || !path || (strlen(path)>=(sizeof(s_path))) ||
     (!is_mp3(path)&&!is_flac(path))) {
    error("Invalid MP3/FLAC SD file");return false;
  }
  if(s_worker.load(std::memory_order_acquire) ||
     vgm_engine_is_playing() || mdx_engine_is_playing() ||
     !vgm_engine_generation_idle() || !vgm_audio_queue_is_flushed()) {
    error("Other audio engine or speaker queue busy");return false;
  }
  snprintf(s_path,sizeof(s_path),"%s",path);
  s_error[0]=0;
  s_stop.store(false,std::memory_order_release);
  s_open_done.store(false,std::memory_order_release);
  s_open_ok.store(false,std::memory_order_release);
  s_source_rate.store(0,std::memory_order_release);
  s_source_channels.store(0,std::memory_order_release);
  s_active.store(true,std::memory_order_release);
  s_worker.store(true,std::memory_order_release);
  s_owned.store(true,std::memory_order_release);
  if(xTaskCreatePinnedToCore(task,"Mp3FlacDecode",16384,nullptr,2,nullptr,0)!=pdPASS) {
    error("MP3/FLAC task creation failed");
    s_active.store(false,std::memory_order_release);
    s_worker.store(false,std::memory_order_release);
    s_owned.store(false,std::memory_order_release);
    return false;
  }
  const uint32_t began=millis();
  while(!s_open_done.load(std::memory_order_acquire) && millis()-began<4000u)
    vTaskDelay(1);
  if(!s_open_done.load(std::memory_order_acquire) ||
     !s_open_ok.load(std::memory_order_acquire)) {
    pcm_file_engine_stop();
    if(!s_error[0])error("MP3/FLAC startup timeout");
    return false;
  }
  return true;
}
void pcm_file_engine_stop() {
  if(!s_owned.load(std::memory_order_acquire))return;
  s_stop.store(true,std::memory_order_release);
  const uint32_t began=millis();
  while(s_worker.load(std::memory_order_acquire) && millis()-began<2500u)
    vTaskDelay(1);
  if(s_worker.load(std::memory_order_acquire)) {
    error("PCM decode task stop timeout - queue retained");
    return; // fail closed: never reuse decoder/PCM slots
  }
  s_active.store(false,std::memory_order_release);
  vgm_audio_queue_cancel_tail_drain();
  if(!vgm_audio_queue_flush_if_idle()) {
    error("Speaker queue not safely flushed");
  } else {
    s_owned.store(false,std::memory_order_release);
  }
}
bool pcm_file_engine_is_playing() {
  return s_active.load(std::memory_order_acquire);
}
bool pcm_file_engine_worker_running() {
  return s_worker.load(std::memory_order_acquire);
}
bool pcm_file_engine_owns_queue() {
  return s_owned.load(std::memory_order_acquire);
}
const char* pcm_file_engine_get_error(){return s_error;}
unsigned pcm_file_engine_source_rate(){return s_source_rate.load();}
unsigned pcm_file_engine_source_channels(){return s_source_channels.load();}
