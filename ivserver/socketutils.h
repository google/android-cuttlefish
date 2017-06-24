#pragma once

#include <inttypes.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <memory>
#include <string>

namespace ivserver {

extern int start_listener_socket(const std::string &path);

extern int connect_to_socket(const std::string &path);

extern int handle_new_connection(const int uds, const bool blocking = true);

template <typename T>
extern bool send_msg(const int uds, const T &msg);
extern bool send_msg(const int uds, const int fd, uint64_t data);

extern std::shared_ptr<std::string> recv_msg(const int uds, const uint32_t len);

extern int recv_msg(const int uds, uint64_t *data);

extern int16_t recv_msg_int16(const int uds);
extern int32_t recv_msg_int32(const int uds);
extern int64_t recv_msg_int64(const int uds);

}  // namespace ivserver
