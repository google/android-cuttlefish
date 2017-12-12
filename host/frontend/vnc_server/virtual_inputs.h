#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VIRTUAL_INPUTS_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VIRTUAL_INPUTS_H_

#include "VirtualInputDevice.h"
#include "vnc_utils.h"

#include <linux/input.h>
#include <android-base/thread_annotations.h>

#include <mutex>

namespace avd {
namespace vnc {

class VirtualInputs {
 public:
  void GenerateKeyPressEvent(int code, bool down);
  void PressPowerButton(bool down);
  void HandlePointerEvent(bool touch_down, int x, int y);

 private:
  std::mutex m_;
  VirtualKeyboard virtual_keyboard_ GUARDED_BY(m_){"remote-keyboard"};
  VirtualTouchPad virtual_touch_pad_ GUARDED_BY(m_){
      "remote-touchpad", ActualScreenWidth(), ActualScreenHeight()};
  VirtualButton virtual_power_button_ GUARDED_BY(m_){"remote-power", KEY_POWER};
};

}  // namespace vnc
}  // namespace avd

#endif
