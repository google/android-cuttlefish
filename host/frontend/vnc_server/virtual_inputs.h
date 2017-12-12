#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VIRTUAL_INPUTS_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VIRTUAL_INPUTS_H_

#include "VirtualInputDevice.h"
#include "vnc_utils.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/threads/thread_annotations.h"

#include <linux/input.h>

#include <mutex>

namespace avd {
namespace vnc {

class VirtualInputs {
 public:
  VirtualInputs();
  ~VirtualInputs();

  void GenerateKeyPressEvent(int code, bool down);
  void PressPowerButton(bool down);
  void HandlePointerEvent(bool touch_down, int x, int y);

 private:
  avd::SharedFD monkey_socket_;
  bool SendMonkeyComand(std::string cmd);
  std::mutex m_;
  VirtualKeyboard virtual_keyboard_ GUARDED_BY(m_);
  VirtualTouchPad virtual_touch_pad_ GUARDED_BY(m_);
  VirtualButton virtual_power_button_ GUARDED_BY(m_);
};

}  // namespace vnc
}  // namespace avd

#endif
