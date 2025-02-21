#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>
#include <sched.h>

#include "MediaQuality.h"

using aidl::android::hardware::tv::mediaquality::impl::MediaQuality;

int main(int /*argc*/, char** /*argv*/) {
  ALOGI("MediaQuality (PPA) starting up...");

  // same as SF main thread
  struct sched_param param = {0};
  param.sched_priority = 2;
  if (sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
    ALOGE("%s: failed to set priority: %s", __FUNCTION__, strerror(errno));
  }

  auto mediaQuality = ndk::SharedRefBase::make<MediaQuality>();
  CHECK(mediaQuality != nullptr);

  const std::string instance =
      std::string() + MediaQuality::descriptor + "/default";
  binder_status_t status = AServiceManager_addService(
      mediaQuality->asBinder().get(), instance.c_str());
  CHECK(status == STATUS_OK);

  // Thread pool for system libbinder (via libbinder_ndk) for aidl services
  // IComposer and IDisplay
  ABinderProcess_setThreadPoolMaxThreadCount(5);
  ABinderProcess_startThreadPool();
  ABinderProcess_joinThreadPool();

  return EXIT_FAILURE;
}
