#include <https/Support.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctype.h>
#include <fcntl.h>
#include <sys/errno.h>

void makeFdNonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { flags = 0; }
    DEBUG_ONLY(int res = )fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    assert(res >= 0);
}

void hexdump(const void *_data, size_t size) {
  const uint8_t *data = static_cast<const uint8_t *>(_data);

  size_t offset = 0;
  while (offset < size) {
    printf("%08zx: ", offset);

    for (size_t col = 0; col < 16; ++col) {
      if (offset + col < size) {
        printf("%02x ", data[offset + col]);
      } else {
        printf("   ");
      }

      if (col == 7) {
        printf(" ");
      }
    }

    printf(" ");

    for (size_t col = 0; col < 16; ++col) {
      if (offset + col < size && isprint(data[offset + col])) {
        printf("%c", data[offset + col]);
      } else if (offset + col < size) {
        printf(".");
      }
    }

    printf("\n");

    offset += 16;
  }
}
