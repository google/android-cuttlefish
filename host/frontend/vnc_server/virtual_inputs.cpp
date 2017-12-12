#include "virtual_inputs.h"
#include <gflags/gflags.h>

#include <mutex>
#include <thread>

DEFINE_string(input_socket, "/tmp/android-cuttlefish-1-input",
              "The name of unix socket where the monkey server is listening "
              "for input commands");

using avd::vnc::VirtualInputs;

VirtualInputs::VirtualInputs()
    : virtual_keyboard_(
          [this](std::string cmd) { return SendMonkeyComand(cmd); }),
      virtual_touch_pad_(
          [this](std::string cmd) { return SendMonkeyComand(cmd); }),
      virtual_power_button_("KEYCODE_POWER", [this](std::string cmd) {
        return SendMonkeyComand(cmd);
      }) {
  monkey_socket_ = avd::SharedFD::SocketLocalClient(FLAGS_input_socket.c_str(),
                                                    false, SOCK_STREAM);
  if (!monkey_socket_->IsOpen()) {
    // Monkey is known to die on the second conection, so let's wait a litttle
    // bit and try again.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    monkey_socket_ = avd::SharedFD::SocketLocalClient(
        FLAGS_input_socket.c_str(), false, SOCK_STREAM);
    if (!monkey_socket_->IsOpen()) {
      LOG(FATAL) << "Unable to connect to the mokey server";
    }
  }
}

namespace {
constexpr char kCmdDone[] = "done\n";
}  // anonymous namespace

VirtualInputs::~VirtualInputs() {
  if (monkey_socket_->IsOpen()) {
    monkey_socket_->Send(kCmdDone, sizeof(kCmdDone) - 1, 0);
  }
}

bool VirtualInputs::SendMonkeyComand(std::string cmd) {
  return monkey_socket_->Send(cmd.c_str(), cmd.size(), 0) == cmd.size();
  // TODO(jemoreira): If monkey is going to be used for a long time it may be
  // useful to check the response to this commands.
}

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
