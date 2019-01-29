#include "host/commands/launch/launch.h"

#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/size_utils.h"
#include "common/vsoc/shm/screen_layout.h"
#include "host/commands/launch/launcher_defs.h"
#include "host/commands/launch/vsoc_shared_memory.h"

using cvd::LauncherExitCodes;

namespace {
cvd::SharedFD CreateIvServerUnixSocket(const std::string& path) {
  return cvd::SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM,
                                          0666);
}
} // namespace

cvd::Command GetIvServerCommand(const vsoc::CuttlefishConfig& config) {
  // Resize gralloc region
  auto actual_width = cvd::AlignToPowerOf2(config.x_res() * 4, 4);// align to 16
  uint32_t screen_buffers_size =
      config.num_screen_buffers() *
      cvd::AlignToPageSize(actual_width * config.y_res() + 16 /* padding */);
  screen_buffers_size +=
      (config.num_screen_buffers() - 1) * 4096; /* Guard pages */

  // TODO(b/79170615) Resize gralloc region too.

  vsoc::CreateSharedMemoryFile(
      config.mempath(),
      {{vsoc::layout::screen::ScreenLayout::region_name, screen_buffers_size}});


  cvd::Command ivserver(config.ivserver_binary());
  ivserver.AddParameter(
      "-qemu_socket_fd=",
      CreateIvServerUnixSocket(config.ivshmem_qemu_socket_path()));
  ivserver.AddParameter(
      "-client_socket_fd=",
      CreateIvServerUnixSocket(config.ivshmem_client_socket_path()));
  return ivserver;
}

// Build the kernel log monitor command. If boot_event_pipe is not NULL, a
// subscription to boot events from the kernel log monitor will be created and
// events will appear on *boot_events_pipe
cvd::Command GetKernelLogMonitorCommand(const vsoc::CuttlefishConfig& config,
                                        cvd::SharedFD* boot_events_pipe) {
  auto log_name = config.kernel_log_socket_name();
  auto server = cvd::SharedFD::SocketLocalServer(log_name.c_str(), false,
                                                 SOCK_STREAM, 0666);
  cvd::Command kernel_log_monitor(config.kernel_log_monitor_binary());
  kernel_log_monitor.AddParameter("-log_server_fd=", server);
  if (boot_events_pipe) {
    cvd::SharedFD pipe_write_end;
    if (!cvd::SharedFD::Pipe(boot_events_pipe, &pipe_write_end)) {
      LOG(ERROR) << "Unable to create boot events pipe: " << strerror(errno);
      std::exit(LauncherExitCodes::kPipeIOError);
    }
    kernel_log_monitor.AddParameter("-subscriber_fd=", pipe_write_end);
  }
  return kernel_log_monitor;
}
