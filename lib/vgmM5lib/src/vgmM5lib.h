#pragma once
// Portable C sound cores shared with Tab5 R67. Linkage must be C for
// globals (g_psg_engine etc) as well as functions: CoreS3's VGM host
// includes its own source headers inside extern "C".
#ifdef __cplusplus
extern "C" {
#endif
#include "fm_engine.h"
#include "pcm_engine.h"
#include "psg_engine.h"
#include "ssg_engine.h"
#include "scc_engine.h"
#include "r31_vgm_bridge.h"
#ifdef __cplusplus
}
#endif
