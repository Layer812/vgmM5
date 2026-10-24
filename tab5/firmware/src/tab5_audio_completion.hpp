#pragma once
// A buffer-release callback only ends Speaker's reference to a PCM pointer.
// It does not by itself mean the playback channel became inactive.
// Pure predicate shared by firmware and its host completion tests.
namespace tab5 {
inline bool audio_output_complete(bool consumer_fence_open,
                                  int ring_live, int ring_pending,
                                  int speaker_borrowed, bool speaker_active) noexcept {
    return consumer_fence_open && ring_live == 0 && ring_pending == 0 &&
           speaker_borrowed == 0 && !speaker_active;
}
} // namespace tab5
