#include "virtual_inputs.h"
#include <mutex>

using avd::vnc::VirtualInputs;

void VirtualInputs::GenerateKeyPressEvent(int code, bool down) {
  std::lock_guard<std::mutex> guard(m_);
  virtual_keyboard_.GenerateKeyPressEvent(code, down);
}

void VirtualInputs::PressPowerButton(bool down) {
  std::lock_guard<std::mutex> guard(m_);
  virtual_power_button_.HandleButtonPressEvent(down);
}

void VirtualInputs::HandlePointerEvent(bool touch_down, int x, int y) {
  std::lock_guard<std::mutex> guard(m_);
  virtual_touch_pad_.HandlePointerEvent(touch_down, x, y);
}
