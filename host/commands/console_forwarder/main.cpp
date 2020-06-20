/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <signal.h>

#include <deque>
#include <thread>
#include <vector>

#include <gflags/gflags.h>
#include <android-base/logging.h>

#include <common/libs/fs/shared_fd.h>
#include <common/libs/fs/shared_select.h>
#include <host/libs/config/cuttlefish_config.h>
#include <host/libs/config/logging.h>

DEFINE_int32(console_in_fd,
             -1,
             "File descriptor for the console's input channel");
DEFINE_int32(console_out_fd,
             -1,
             "File descriptor for the console's output channel");

// Handles forwarding the serial console to a socket.
// It receives the socket fd along with a couple of fds for the console (could
// be the same fd twice if, for example a socket_pair were used).
// Data available in the console's output needs to be read immediately to avoid
// the having the VMM blocked on writes to the pipe. To achieve this one thread
// takes care of (and only of) all read calls (from console output and from the
// socket client), using select(2) to ensure it never blocks. Writes are handled
// in a different thread, the two threads communicate through a buffer queue
// protected by a mutex.
class ConsoleForwarder {
 public:
  ConsoleForwarder(cvd::SharedFD socket,
                   cvd::SharedFD console_in,
                   cvd::SharedFD console_out,
                   cvd::SharedFD console_log) : socket_(socket),
                                                console_in_(console_in),
                                                console_out_(console_out),
                                                console_log_(console_log) {}
  [[noreturn]] void StartServer() {
    // Create a new thread to handle writes to the console and to the any client
    // connected to the socket.
    writer_thread_ = std::thread([this]() { WriteLoop(); });
    // Use the calling thread (likely the process' main thread) to handle
    // reading the console's output and input from the client.
    ReadLoop();
  }
 private:
  void EnqueueWrite(std::shared_ptr<std::vector<char>> buf_ptr, cvd::SharedFD fd) {
    std::lock_guard<std::mutex> lock(write_queue_mutex_);
    write_queue_.emplace_back(fd, std::move(buf_ptr));
    condvar_.notify_one();
  }

  [[noreturn]] void WriteLoop() {
    while (true) {
      while (!write_queue_.empty()) {
        std::shared_ptr<std::vector<char>> buf_ptr;
        cvd::SharedFD fd;
        {
          std::lock_guard<std::mutex> lock(write_queue_mutex_);
          auto& front = write_queue_.front();
          buf_ptr = std::move(front.second);
          fd = front.first;
          write_queue_.pop_front();
        }
        // Write all bytes to the file descriptor. Writes may block, so the
        // mutex lock should NOT be held while writing to avoid blocking the
        // other thread.
        ssize_t bytes_written = 0;
        ssize_t bytes_to_write = buf_ptr->size();
        while (bytes_to_write > 0) {
          bytes_written =
              fd->Write(buf_ptr->data() + bytes_written, bytes_to_write);
          if (bytes_written < 0) {
            LOG(ERROR) << "Error writing to fd: " << fd->StrError();
            // Don't try to write from this buffer anymore, error handling will
            // be done on the reading thread (failed client will be
            // disconnected, on serial console failure this process will abort).
            break;
          }
          bytes_to_write -= bytes_written;
        }
      }
      {
        std::unique_lock<std::mutex> lock(write_queue_mutex_);
        // Check again before sleeping, state may have changed
        if (write_queue_.empty()) {
          condvar_.wait(lock);
        }
      }
    }
  }

  [[noreturn]] void ReadLoop() {
    cvd::SharedFD client_fd;
    while (true) {
      cvd::SharedFDSet read_set;
      if (client_fd->IsOpen()) {
        read_set.Set(client_fd);
      } else {
        read_set.Set(socket_);
      }
      read_set.Set(console_out_);
      cvd::Select(&read_set, nullptr, nullptr, nullptr);
      if (read_set.IsSet(console_out_)) {
        std::shared_ptr<std::vector<char>> buf_ptr = std::make_shared<std::vector<char>>(4096);
        auto bytes_read = console_out_->Read(buf_ptr->data(), buf_ptr->size());
        if (bytes_read <= 0) {
          LOG(ERROR) << "Error reading from console output: "
                     << console_out_->StrError();
          // This is likely unrecoverable, so exit here
          std::exit(-4);
        }
        buf_ptr->resize(bytes_read);
        EnqueueWrite(buf_ptr, console_log_);
        if (client_fd->IsOpen()) {
          EnqueueWrite(buf_ptr, client_fd);
        }
      }
      if (read_set.IsSet(socket_)) {
        // socket_ will only be included in the select call (and therefore only
        // present in the read set) if there is no client connected, so this
        // assignment is safe.
        client_fd = cvd::SharedFD::Accept(*socket_);
        if (!client_fd->IsOpen()) {
          LOG(ERROR) << "Error accepting connection on socket: "
                     << client_fd->StrError();
        }
      }
      if (read_set.IsSet(client_fd)) {
        std::shared_ptr<std::vector<char>> buf_ptr = std::make_shared<std::vector<char>>(4096);
        auto bytes_read = client_fd->Read(buf_ptr->data(), buf_ptr->size());
        if (bytes_read <= 0) {
          LOG(ERROR) << "Error reading from client fd: "
                     << client_fd->StrError();
          client_fd->Close(); // ignore errors here
        } else {
          buf_ptr->resize(bytes_read);
          EnqueueWrite(buf_ptr, console_in_);
        }
      }
    }
  }

  cvd::SharedFD socket_;
  cvd::SharedFD console_in_;
  cvd::SharedFD console_out_;
  cvd::SharedFD console_log_;
  std::thread writer_thread_;
  std::mutex write_queue_mutex_;
  std::condition_variable condvar_;
  std::deque<std::pair<cvd::SharedFD, std::shared_ptr<std::vector<char>>>> write_queue_;
};

int main(int argc, char** argv) {
  cvd::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_console_in_fd < 0 || FLAGS_console_out_fd < 0) {
    LOG(ERROR) << "Invalid file descriptors: " << FLAGS_console_in_fd << ", "
               << FLAGS_console_out_fd;
    return -1;
  }

  auto console_in = cvd::SharedFD::Dup(FLAGS_console_in_fd);
  close(FLAGS_console_in_fd);
  if (!console_in->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_console_in_fd << ": "
               << console_in->StrError();
    return -2;
  }
  close(FLAGS_console_in_fd);

  auto console_out = cvd::SharedFD::Dup(FLAGS_console_out_fd);
  close(FLAGS_console_out_fd);
  if (!console_out->IsOpen()) {
    LOG(ERROR) << "Error dupping fd " << FLAGS_console_out_fd << ": "
               << console_out->StrError();
    return -2;
  }

  auto config = vsoc::CuttlefishConfig::Get();
  if (!config) {
    LOG(ERROR) << "Unable to get config object";
    return -3;
  }

  auto instance = config->ForDefaultInstance();
  auto console_socket_name = instance.console_path();
  auto socket = cvd::SharedFD::SocketLocalServer(console_socket_name.c_str(),
                                                 false,
                                                 SOCK_STREAM,
                                                 0600);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Failed to create console socket at " << console_socket_name
               << ": " << socket->StrError();
    return -5;
  }

  auto console_log = instance.PerInstancePath("console_log");
  auto console_log_fd = cvd::SharedFD::Open(console_log.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0666);
  ConsoleForwarder console_forwarder(socket, console_in, console_out, console_log_fd);

  // Don't get a SIGPIPE from the clients
  if (sigaction(SIGPIPE, nullptr, nullptr) != 0) {
    LOG(FATAL) << "Failed to set SIGPIPE to be ignored: " << strerror(errno);
    return -6;
  }

  console_forwarder.StartServer();
}
