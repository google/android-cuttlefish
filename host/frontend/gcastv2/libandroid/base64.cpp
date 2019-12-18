#include <media/stagefright/foundation/base64.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>

namespace android {

sp<ABuffer> decodeBase64(const std::string_view &s) {
    if ((s.size() % 4) != 0) {
        return NULL;
    }

    size_t n = s.size();
    size_t padding = 0;
    if (n >= 1 && s[n - 1] == '=') {
        padding = 1;

        if (n >= 2 && s[n - 2] == '=') {
            padding = 2;
        }
    }

    size_t outLen = 3 * s.size() / 4 - padding;

    sp<ABuffer> buffer = new ABuffer(outLen);

    uint8_t *out = buffer->data();
    size_t j = 0;
    uint32_t accum = 0;
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        unsigned value;
        if (c >= 'A' && c <= 'Z') {
            value = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            value = 26 + c - 'a';
        } else if (c >= '0' && c <= '9') {
            value = 52 + c - '0';
        } else if (c == '+') {
            value = 62;
        } else if (c == '/') {
            value = 63;
        } else if (c != '=') {
            return NULL;
        } else {
            if (i < n - padding) {
                return NULL;
            }

            value = 0;
        }

        accum = (accum << 6) | value;

        if (((i + 1) % 4) == 0) {
            out[j++] = (accum >> 16);

            if (j < outLen) { out[j++] = (accum >> 8) & 0xff; }
            if (j < outLen) { out[j++] = accum & 0xff; }

            accum = 0;
        }
    }

    return buffer;
}

static char encode6Bit(unsigned x) {
    if (x <= 25) {
        return 'A' + x;
    } else if (x <= 51) {
        return 'a' + x - 26;
    } else if (x <= 61) {
        return '0' + x - 52;
    } else if (x == 62) {
        return '+';
    } else {
        return '/';
    }
}

void encodeBase64(const void *_data, size_t size, std::string *out) {
    out->clear();

    const uint8_t *data = (const uint8_t *)_data;

    size_t i;
    for (i = 0; i < (size / 3) * 3; i += 3) {
        uint8_t x1 = data[i];
        uint8_t x2 = data[i + 1];
        uint8_t x3 = data[i + 2];

        out->append(1, encode6Bit(x1 >> 2));
        out->append(1, encode6Bit((x1 << 4 | x2 >> 4) & 0x3f));
        out->append(1, encode6Bit((x2 << 2 | x3 >> 6) & 0x3f));
        out->append(1, encode6Bit(x3 & 0x3f));
    }
    switch (size % 3) {
        case 0:
            break;
        case 2:
        {
            uint8_t x1 = data[i];
            uint8_t x2 = data[i + 1];
            out->append(1, encode6Bit(x1 >> 2));
            out->append(1, encode6Bit((x1 << 4 | x2 >> 4) & 0x3f));
            out->append(1, encode6Bit((x2 << 2) & 0x3f));
            out->append(1, '=');
            break;
        }
        default:
        {
            uint8_t x1 = data[i];
            out->append(1, encode6Bit(x1 >> 2));
            out->append(1, encode6Bit((x1 << 4) & 0x3f));
            out->append("==");
            break;
        }
    }
}

}  // namespace android
