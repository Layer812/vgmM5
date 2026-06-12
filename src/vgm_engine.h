#pragma once
#include <Arduino.h>

void vgm_engine_init(void);
bool vgm_engine_play(const char* filepath, bool use_sd);
void vgm_engine_stop(void);
void vgm_engine_toggle_pause(void);
bool vgm_engine_is_playing(void);
void vgm_engine_set_volume(uint8_t vol);
const char* vgm_engine_get_title(void);
const char* vgm_engine_get_error(void);
