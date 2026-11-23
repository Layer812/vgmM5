#pragma once
// Tab5 R68 microSD MP3/FLAC -> existing 44.1 kHz stereo PCM ring.
// No new Speaker, I2S, codecs, partitions, or audio transport.
bool pcm_file_engine_play(const char* filepath, bool use_sd);
void pcm_file_engine_stop();
bool pcm_file_engine_is_playing();
bool pcm_file_engine_worker_running();
bool pcm_file_engine_owns_queue();
const char* pcm_file_engine_get_error();
unsigned pcm_file_engine_source_rate();
unsigned pcm_file_engine_source_channels();
