#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

// This is a simple vsock 'sibling' tester. It's used to verify
// vsock communications between two VMs on a host.

#define BUFSIZE 1024  // for storing some temp strings for testing

// Checks a socket handle, and prints error / quits if failure
int check_socket(char *name, int fd) {
  if (fd < 0) {
    fprintf(stderr, "error initializing socket: %s: %s\n", name,
            strerror(errno));
    exit(0);
  }
  return fd;
}

// Checks error code is nonzero, and if so, print a message,
// closing a socket handle if provided as well.
int check_error(char *operation, int result, int fd) {
  if (result != 0) {
    fprintf(stderr, "error for operation %s: %s\n", operation, strerror(errno));
    if (fd != -1) close(fd);
    exit(0);
  }
  return result;
}

// Main execution path for server test mode. This mode runs
// a listener for vsock socket on the specified port, then
// prints the value received before echoing the same value
// back to the client for testing.
int main_server(int port) {
  printf("Starting a vsock server on port %d\n", port);

  struct sockaddr_vm sa_listen = {
      .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_ANY, .svm_port = port};
  struct sockaddr_vm sa_client;
  socklen_t socklen_client = sizeof(sa_client);

  int listen_fd = check_socket("listen_fd", socket(AF_VSOCK, SOCK_STREAM, 0));

  check_error("binding main listen socket",
              bind(listen_fd, (struct sockaddr *)&sa_listen, sizeof(sa_listen)),
              listen_fd);

  check_error("listen on main socket", listen(listen_fd, 1), listen_fd);

  int client_fd =
      accept(listen_fd, (struct sockaddr *)&sa_client, &socklen_client);

  check_error("accept() on main socket", client_fd < 0, listen_fd);

  fprintf(stderr, "Connection from cid %u port %u...\n", sa_client.svm_cid,
          sa_client.svm_port);

  close(listen_fd);

  char buf[BUFSIZE];
  memset(buf, 0, BUFSIZE);
  int len = read(client_fd, buf, BUFSIZE - 1);

  printf("Read %d bytes, str is '%s':\n", len, buf);

  printf("Echoing back data...\n");

  write(client_fd, buf, len);

  printf("Data sent.\n");

  close(client_fd);

  return 0;
}

// Main execution path for 'client' test mode. This mode
// connects to specified vsock cid and port, and sends a string
// to a 'server', which is a peer listening on specified vsock port.
// Client mode also waits for server to echo back the same
// value and prints this when received.
int main_client(int cid, int port, char *str) {
  struct sockaddr_vm sa = {.svm_family = AF_VSOCK,
                           .svm_flags = VMADDR_FLAG_TO_HOST,
                           .svm_cid = cid,
                           .svm_port = port};

  printf("Connecting to cid %d port %d\n", cid, port);

  int fd = check_socket("main socket", socket(AF_VSOCK, SOCK_STREAM, 0));

  check_error("connect", connect(fd, (struct sockaddr *)&sa, sizeof(sa)), fd);

  printf("Connected, sending data '%s' to server...\n", str);

  write(fd, str, strlen(str));

  printf("Data sent.  Waiting for response...\n");

  char buf[BUFSIZE];
  memset(buf, 0, BUFSIZE);
  int len = read(fd, buf, BUFSIZE - 1);

  printf("Read %d bytes back from server, str is '%s':\n", len, buf);

  close(fd);

  return 0;
}

#define safer_atoi(x) strtol(x, NULL, 10)

int main(int argc, char *argv[]) {
  if (argc == 2) {
    main_server(safer_atoi(argv[1]));
  } else if (argc == 4) {
    main_client(safer_atoi(argv[1]), safer_atoi(argv[2]), argv[3]);
  } else {
    printf(
        "Welcome to vsock-test! This utility helps test/verify "
        "'sibling' (vm to vm) vsock comms.\n\n"
        "Please run this command via one of the 2 following forms:\n\n"
        "\tvsock-test [port]\n"
        "\t\tThis format runs a vsock server, where [port] is the "
        "vsock port to listen on.\n\n"
        "\tvsock-test [cid] [port] [str]\n"
        "\t\tThis format runs a vsock client, where:\n"
        "\t\t\t[cid] is the CID of server to connect to\n"
        "\t\t\t[port] is vsock port to connect to\n"
        "\t\t\t[str] is any string to send from client for testing\n\n");
  }
}
