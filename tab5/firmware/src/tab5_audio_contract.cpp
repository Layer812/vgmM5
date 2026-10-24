#include "tab5_audio_contract.hpp"
static_assert(tab5::valid(tab5::kOriginalStereo44100), "Original PCM contract changed");
static_assert(tab5::payload_bytes(tab5::kOriginalStereo44100)==2048, "Stereo payload mismatch");
static_assert(!tab5::ready_for_physical_audio(tab5::kOriginalStereo44100), "Tab5 audio has not been verified");
