#ifndef VGMM5_NUTTX_CODEC_H
#define VGMM5_NUTTX_CODEC_H
// Portable POSIX decoder for NuttX/ESP32-P4. NO codec/I2S/flash side effects.
// Connect vgmm5_pcm_sink() to the EXISTING Pyxel pxaudio/I2S mixer in an
// adapter; decoder never touches ES8388 hardware, DMA or device nodes.
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { VGMM5_CODEC_OK=0, VGMM5_CODEC_STOPPED=1,
       VGMM5_CODEC_BAD_FILE=-1, VGMM5_CODEC_UNSUPPORTED=-2,
       VGMM5_CODEC_NO_MEMORY=-3, VGMM5_CODEC_IO=-4 };
typedef struct {
  uint32_t source_hz;
  uint32_t output_hz;
  uint8_t channels;       // original source channels: 1 or 2
  uint8_t flac;           // 0=MP3, 1=FLAC
  uint64_t frames_output;
} vgmm5_decode_stats;
typedef int (*vgmm5_pcm_sink)(
    void *opaque, const int16_t *interleaved_stereo, size_t frames,
    uint32_t sample_rate);
typedef int (*vgmm5_stop_requested)(void *opaque);
// Synchronous streaming call. 512 stereo frames per callback, rate=output_hz.
// Returns 0 on natural EOF; 1 when requested to stop; negatives on failure.
// Stereo 16-bit signed PCM only, no packed bytes, no implicit DMA ownership.
// 8..96 kHz input, 1/2 channels. MP3 rate is per decoded frame.
// Caller serializes playback against existing pxaudio producer; never run two
// independent producers into a single DMA ring.
int vgmm5_decode_file(const char *path, uint32_t output_hz,
                      vgmm5_pcm_sink sink, vgmm5_stop_requested stop,
                      void *opaque, vgmm5_decode_stats *stats);
#ifdef __cplusplus
}
#endif
#endif
