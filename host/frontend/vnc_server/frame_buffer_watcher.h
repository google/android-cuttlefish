#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_FRAME_BUFFER_WATCHER_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_FRAME_BUFFER_WATCHER_H_

#include "blackboard.h"
#include "jpeg_compressor.h"
#include "simulated_hw_composer.h"

#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace avd {
namespace vnc {
class FrameBufferWatcher {
 public:
  explicit FrameBufferWatcher(BlackBoard* bb);
  FrameBufferWatcher(const FrameBufferWatcher&) = delete;
  FrameBufferWatcher& operator=(const FrameBufferWatcher&) = delete;
  ~FrameBufferWatcher();

  StripePtrVec StripesNewerThan(ScreenOrientation orientation,
                                const SeqNumberVec& seq_num) const;
  static int StripesPerFrame();

 private:
  static Stripe Rotated(Stripe stripe);

  bool closed() const;
  bool StripeIsDifferentFromPrevious(const Stripe& stripe) const
      REQUIRES(stripes_lock_);
  // returns true if stripe is still considered new and seq number was updated
  bool UpdateMostRecentSeqNumIfStripeIsNew(const Stripe& stripe)
      REQUIRES(stripes_lock_);
  // returns true if stripe is still considered new and was updated
  bool UpdateStripeIfStripeIsNew(const std::shared_ptr<const Stripe>& stripe)
      EXCLUDES(stripes_lock_);
  // Compresses stripe->raw_data to stripe->jpeg_data
  void CompressStripe(JpegCompressor* jpeg_compressor, Stripe* stripe);
  void Worker();
  void Updater();

  StripePtrVec& Stripes(ScreenOrientation orientation) REQUIRES(stripes_lock_);
  const StripePtrVec& Stripes(ScreenOrientation orientation) const
      REQUIRES(stripes_lock_);

  std::vector<std::thread> workers_;
  mutable std::mutex stripes_lock_;
  std::array<StripePtrVec, kNumOrientations> stripes_ GUARDED_BY(stripes_lock_);
  SeqNumberVec most_recent_identical_stripe_seq_nums_
      GUARDED_BY(stripes_lock_) = MakeSeqNumberVec();
  mutable std::mutex m_;
  bool closed_ GUARDED_BY(m_){};
  BlackBoard* bb_{};
  SimulatedHWComposer hwcomposer{bb_};
};

}  // namespace vnc
}  // namespace avd

#endif
