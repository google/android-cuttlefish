#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "vm_sockets.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define BUFFER_SIZE 4096
#define CLIENT_QUEUE_SIZE 100

int fwd_cid;
int fwd_port;

int setupServerSocket(sockaddr_vm& addr) {
  int vsock_socket = socket(AF_VSOCK, SOCK_STREAM, 0);

  if (vsock_socket == -1) {
    printf("Failed to create server VSOCK socket, ERROR = %s\n",
           strerror(errno));
    return -1;
  }

  if (bind(vsock_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
      0) {
    printf("Failed to bind to server VSOCK socket, ERROR = %s\n",
           strerror(errno));
    return -1;
  }

  if (listen(vsock_socket, CLIENT_QUEUE_SIZE) != 0) {
    printf("Failed to listen on server VSOCK socket, ERROR = %s\n",
           strerror(errno));
    return -1;
  }

  return vsock_socket;
}

void closeFd(int fd) {
  close(fd);
  shutdown(fd, SHUT_RDWR);
}

bool transfer(int src_fd, int dst_fd) {
  char buf[BUFFER_SIZE];
  int valread = read(src_fd, buf, BUFFER_SIZE);
  if (valread <= 0) {
    return true;
  }
  int write_val = write(dst_fd, buf, valread);
  return write_val < 0;
}

void* handleConnection(void* args) {
  int client_sock = *((int*)args);
  int server_sock = socket(AF_VSOCK, SOCK_STREAM, 0);

  if (server_sock < 0) {
    printf("Failed to create forwarding VSOCK socket, ERROR = %s\n",
           strerror(errno));
    closeFd(client_sock);
    delete ((int*)args);
    return nullptr;
  }

  sockaddr_vm fwd_addr{};
  fwd_addr.svm_family = AF_VSOCK;
  fwd_addr.svm_cid = fwd_cid;
  fwd_addr.svm_port = fwd_port;

  if (connect(server_sock, reinterpret_cast<sockaddr*>(&fwd_addr),
              sizeof(fwd_addr)) < 0) {
    printf("Failed to connect to forwarding vsock socket, ERROR = %s\n",
           strerror(errno));
    closeFd(server_sock);
    closeFd(client_sock);
    delete ((int*)args);
    return nullptr;
  }

  bool disconnected = false;
  while (!disconnected) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(client_sock, &rfds);
    FD_SET(server_sock, &rfds);

    int rv = select(MAX(client_sock, server_sock) + 1, &rfds, nullptr, nullptr,
                    nullptr);
    if (rv == -1) {
      printf("ERROR in Select!. Error = %s\n", strerror(errno));
      break;
    }

    if (FD_ISSET(client_sock, &rfds)) {
      disconnected = transfer(client_sock, server_sock);
    }

    if (FD_ISSET(server_sock, &rfds)) {
      disconnected = transfer(server_sock, client_sock);
    }
  }

  closeFd(client_sock);
  closeFd(server_sock);

  delete ((int*)args);
  return nullptr;
}

int main(int argc, char** argv) {
  if (argc != 4) {
    printf(
        "Wrong usage of proxy. Please enter:\n 1) The port number to be used "
        "for the proxy\n2) The CID of the service to which requests are "
        "forwarded\n3) The port of the service to which requests are "
        "forwarded\n");
    return -1;
  }

  sockaddr_vm addr{};
  addr.svm_family = AF_VSOCK;
  addr.svm_cid = 2;
  addr.svm_port = atoi(argv[1]);

  fwd_cid = atoi(argv[2]);
  fwd_port = atoi(argv[3]);

  int proxy_socket = setupServerSocket(addr);

  if (proxy_socket == -1) {
    printf("Failed to set up proxy server VSOCK socket, ERROR = %s\n",
           strerror(errno));
    return -1;
  }

  int client_sock;
  int len = sizeof(addr);

  while (true) {
    if ((client_sock = accept(proxy_socket, reinterpret_cast<sockaddr*>(&addr),
                              reinterpret_cast<socklen_t*>(&len))) < 0) {
      printf("Failed to accept VSOCK connection, ERROR = %s\n",
             strerror(errno));
      closeFd(client_sock);
      continue;
    }

    pthread_t thread_id;
    int* client_arg = new int(client_sock);

    pthread_create(&thread_id, nullptr, handleConnection, (void*)client_arg);
    pthread_detach(thread_id);
  }

  closeFd(proxy_socket);
  return 0;
}
