#pragma once
#include <atomic>
// OPT6: one atomic state makes close()/enter() linearizable. Two separate
// atomics (closed and busy) could permit a late entrant after close saw idle.
class Tab5AudioConsumerFence {
public:
 bool enter() {
  unsigned expected = 0;
  return state_.compare_exchange_strong(expected, kBusy,
         std::memory_order_acq_rel, std::memory_order_acquire);
 }
 void leave() { state_.fetch_and(~kBusy, std::memory_order_acq_rel); }
 void close() { state_.fetch_or(kClosed, std::memory_order_acq_rel); }
 void reopen() { state_.store(0, std::memory_order_release); } // only when idle
 bool idle() const { return !(state_.load(std::memory_order_acquire) & kBusy); }
 bool closed() const { return (state_.load(std::memory_order_acquire) & kClosed) != 0; }
private:
 static constexpr unsigned kClosed = 1, kBusy = 2;
 std::atomic<unsigned> state_{0};
};
