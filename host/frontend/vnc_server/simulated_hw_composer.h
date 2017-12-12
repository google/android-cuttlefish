#pragma once

#include "blackboard.h"

#include "common/libs/thread_safe_queue/thread_safe_queue.h"
#include "common/libs/threads/thread_annotations.h"
#include "common/vsoc/framebuffer/fb_bcast_region.h"

#include <mutex>
#include <thread>

#include <condition_variable>
#ifdef FUZZ_TEST_VNC
#include <random>
#endif

namespace avd {
namespace vnc {
class SimulatedHWComposer {
 public:
  SimulatedHWComposer(BlackBoard* bb);
  SimulatedHWComposer(const SimulatedHWComposer&) = delete;
  SimulatedHWComposer& operator=(const SimulatedHWComposer&) = delete;
  ~SimulatedHWComposer();

  Stripe GetNewStripe();

  // NOTE not constexpr on purpose
  static int NumberOfStripes();

 private:
  bool closed();
  void close();
  static void EraseHalfOfElements(ThreadSafeQueue<Stripe>::QueueImpl* q);
  void MakeStripes();

#ifdef FUZZ_TEST_VNC
  std::default_random_engine engine_;
  std::uniform_int_distribution<int> random_ =
      std::uniform_int_distribution<int>{0, 2};
#endif
  static constexpr int kNumStripes = 8;
  constexpr static std::size_t kMaxQueueElements = 64;
  bool closed_ GUARDED_BY(m_){};
  std::mutex m_;
  vsoc::framebuffer::FBBroadcastRegion* fb_region_;
  BlackBoard* bb_{};
  ThreadSafeQueue<Stripe> stripes_;
  std::thread stripe_maker_;
};
}  // namespace vnc
}  // namespace avd
