
#include "host/ivserver/socketutils.h"

#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <string>

static int test_client(const std::string &region) {
  auto clientsock = connect_to_socket("/tmp/ivshmem_client_socket");
  if (clientsock == -1) {
    perror("Error in connecting to the client socket");
    return 1;
  }

  auto protocol_version = recv_msg_int32(clientsock);
  std::cout << "protocol version: " << protocol_version << std::endl;
  uint16_t region_name_sz = region.length();
  if (send_msg(clientsock, static_cast<uint16_t>(region_name_sz)) == -1) {
    perror("Error in sending region name size");
    close(clientsock);
    return 1;
  }

  std::cout << "sending region name : " << region << std::endl;

  if (send_msg(clientsock, region) == -1) {
    perror("error in sending region name: ");
    close(clientsock);
    return 1;
  }

  auto offset = recv_msg_int32(clientsock);
  if (offset == -1) {
    std::cout << "region : " << region << " not found" << std::endl;
    close(clientsock);
    return 1;
  }

  std::cout << "begin_offset: " << offset << std::endl;

  offset = recv_msg_int32(clientsock);
  std::cout << "end_offset: " << offset << std::endl;

  uint64_t data = 0;
  auto event = recv_msg(clientsock, &data);
  std::cout << "guest_to_host_eventfd " << event << std::endl;

  event = recv_msg(clientsock, &data);
  std::cout << "host_to_guest_eventfd " << event << std::endl;

  close(clientsock);
  return 0;
}

int main(int argc, char **argv) {
  std::string regions[] = {
      "hwcomposer", "misc", "sensors", "darkmatter",
  };

  for (const auto &region : regions) {
    auto status = test_client(region);
    if (region == "darkmatter") {
      if (status == 0)
        std::cout << "negative test failed" << std::endl;
      else
        std::cout << "negative test passed" << std::endl;
    } else if (status) {
      std::cout << "test failed" << std::endl;
    }
  }

  return 0;
}
