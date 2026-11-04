#pragma once
// Portable VGM sound engines, sourced from field-proven Tab5 R67.
// No M5Unified, ESP32-P4 SPM, display, audio DMA, UI or platform task APIs.
// App supplies its own chip routing, 44.1kHz pacing, mixer and device output.
#include "fm_engine.h"
#include "pcm_engine.h"
#include "psg_engine.h"
#include "ssg_engine.h"
#include "scc_engine.h"
#include "r31_vgm_bridge.h"
