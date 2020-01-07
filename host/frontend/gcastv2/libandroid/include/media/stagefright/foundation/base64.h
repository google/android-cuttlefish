#ifndef BASE_64_H_

#define BASE_64_H_

#include <utils/RefBase.h>

#include <string>
#include <string_view>

namespace android {

struct ABuffer;

sp<ABuffer> decodeBase64(const std::string_view &s);
void encodeBase64(const void *data, size_t size, std::string *out);

}  // namespace android

#endif  // BASE_64_H_
