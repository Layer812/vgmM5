#pragma once
#include <atomic>
#include <cstdint>

// OPT6: actual Speaker_Class buffer release, not a guessed 4-buffer delay.
// Exactly one publisher (VGM or MDX), one consumer and one Speaker callback.
// The producer advances only when live < capacity-reserve; callback marks
// ownership returned, but only the consumer / fenced controller retires slots.
// FIFO retirement handles possible out-of-order release notifications.
class Tab5AudioQueueAccounting {
public:
 static constexpr unsigned kSlots = 16;
 static constexpr unsigned kReserve = 4;
 void publish(std::atomic<int>& live) {
  live.fetch_add(1, std::memory_order_relaxed);
  pending_.fetch_add(1, std::memory_order_release);
 }
 bool has_pending() const { return pending_.load(std::memory_order_acquire) > 0; }
 int pending() const { return pending_.load(std::memory_order_acquire); }
 int outstanding() const { return outstanding_.load(std::memory_order_acquire); }

 // Called by the audio consumer BEFORE Speaker.playRaw; a release may arrive
 // before playRaw returns. Failure never produces a release callback.
 void begin_submit(unsigned slot) {
  states_[slot].store(1, std::memory_order_release);
  outstanding_.fetch_add(1, std::memory_order_acq_rel);
 }
 void finish_submit(unsigned slot, bool queued, std::atomic<int>& live) {
  if (queued) {
   pending_.fetch_sub(1, std::memory_order_acq_rel);
  } else {
   states_[slot].store(0, std::memory_order_release);
   outstanding_.fetch_sub(1, std::memory_order_acq_rel);
  }
  reclaim(live);
 }
 // Called from Speaker's task. Never waits, allocates, logs or calls Speaker.
 void released(unsigned slot) {
  states_[slot].store(2, std::memory_order_release);
  outstanding_.fetch_sub(1, std::memory_order_acq_rel);
 }
 // Only one control context at a time (consumer, or controller when fenced).
 void reclaim(std::atomic<int>& live) {
  for (;;) {
   uint8_t expected = 2;
   if (!states_[retire_].compare_exchange_strong(
        expected, 0, std::memory_order_acq_rel)) break;
   retire_ = (retire_ + 1) % kSlots;
   live.fetch_sub(1, std::memory_order_release);
  }
 }
 // Only after consumer fence is idle and ALL speaker callbacks delivered.
 void reset(std::atomic<int>& live) {
  for (auto& state: states_) state.store(0, std::memory_order_release);
  retire_ = 0;
  pending_.store(0, std::memory_order_release);
  outstanding_.store(0, std::memory_order_release);
  live.store(0, std::memory_order_release);
 }
private:
 std::atomic<int> pending_{0};
 std::atomic<int> outstanding_{0};
 std::atomic<uint8_t> states_[kSlots]{}; // 0 not submitted, 1 borrowed, 2 released
 unsigned retire_ = 0; // touched only by the consumer or fenced controller
};
