#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_SIMULATED_HW_COMPOSER_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_SIMULATED_HW_COMPOSER_H_

#include "blackboard.h"

#include <GceFrameBuffer.h>
#include <GceFrameBufferControl.h>
#include <ThreadSafeQueue.hpp>

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
  GceFrameBufferControl& control_;
  BlackBoard* bb_{};
  ThreadSafeQueue<Stripe> stripes_;
  std::thread stripe_maker_;
  char* frame_buffer_memory_{};
  int frame_buffer_fd_{};
};
}  // namespace vnc
}  // namespace avd

#endif
