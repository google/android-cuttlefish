#include <android-base/logging.h>
#include <android-base/strings.h>
#include <sys/socket.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "host/libs/config/cuttlefish_config.h"

// Messages are always 128 bytes.
#define MESSAGE_SIZE 128

using cuttlefish::SharedFD;

int main(int argc, char** argv) {
  if (argc <= 1) {
    return 1;
  }

  // Connect to WebRTC
  int fd = std::atoi(argv[1]);
  LOG(INFO) << "Connecting to WebRTC server...";
  SharedFD webrtc_socket = SharedFD::Dup(fd);
  close(fd);
  if (webrtc_socket->IsOpen()) {
    LOG(INFO) << "Connected";
  } else {
    LOG(ERROR) << "Could not connect, exiting...";
    return 1;
  }

  // Track state for our two commands.
  bool statusbar_expanded = false;
  bool dnd_on = false;

  char buf[MESSAGE_SIZE];
  while (1) {
    // Read the command message from the socket.
    if (!webrtc_socket->IsOpen()) {
      LOG(WARNING) << "WebRTC was closed.";
      break;
    }
    if (cuttlefish::ReadExact(webrtc_socket, buf, MESSAGE_SIZE) !=
        MESSAGE_SIZE) {
      LOG(WARNING) << "Failed to read the correct number of bytes.";
      break;
    }
    auto split = android::base::Split(buf, ":");
    std::string command = split[0];
    std::string state = split[1];

    // Ignore button-release events, when state != down.
    if (state != "down") {
      continue;
    }

    // Demonstrate two commands. For demonstration purposes these two
    // commands use adb shell, but commands can execute any action you choose.
    std::string adb_shell_command =
        cuttlefish::HostBinaryPath("adb");
    if (command == "settings") {
      adb_shell_command += " shell cmd statusbar ";
      adb_shell_command += statusbar_expanded ? "collapse" : "expand-settings";
      statusbar_expanded = !statusbar_expanded;
    } else if (command == "alert") {
      adb_shell_command += " shell cmd notification set_dnd ";
      adb_shell_command += dnd_on ? "off" : "on";
      dnd_on = !dnd_on;
    } else {
      LOG(WARNING) << "Unexpected command: " << buf;
    }

    if (!adb_shell_command.empty()) {
      if (system(adb_shell_command.c_str()) != 0) {
        LOG(ERROR) << "Failed to run command: " << adb_shell_command;
      }
    }
  }
}
