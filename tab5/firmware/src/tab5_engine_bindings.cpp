// Compile only when the original vgmM5 engine sources have been integrated.
// DO NOT add this to the current standalone R4/R5 UI build until the engine's
// ESP32-P4 audio-output path has been audited and made compatible.
#ifdef TAB5_WITH_REAL_AUDIO
#include "tab5_engine_bridge.hpp"
#include "vgm_engine.h"
#include "mdx_engine.hpp"
namespace tab5 {
EngineApi make_real_engine_api(){
 return {vgm_engine_play,mdx_engine_play,vgm_engine_stop,mdx_engine_stop,
         vgm_engine_is_playing,mdx_engine_is_playing,vgm_engine_set_volume,
         vgm_engine_generation_idle,vgm_audio_queue_is_flushed};
}
} // namespace tab5
#endif
