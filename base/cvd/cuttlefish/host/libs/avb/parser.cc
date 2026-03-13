//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/libs/avb/parser.h"

#include <string>
#include <utility>

#include "libavb/libavb.h"

#include "cuttlefish/io/io.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

/* static */ Result<AvbParser> AvbParser::Parse(ReaderSeeker& source) {
  uint64_t offset = CF_EXPECT(source.SeekEnd(-sizeof(AvbFooter)));
  CF_EXPECT_GT(offset, 0, "Source is too short");
  AvbFooter footer_in = CF_EXPECT(ReadExactBinary<AvbFooter>(source));
  AvbFooter footer;
  CF_EXPECT(avb_footer_validate_and_byteswap(&footer_in, &footer));

  std::vector<uint8_t> vbmeta(footer.vbmeta_size);
  CF_EXPECT(PReadExact(source, reinterpret_cast<char*>(vbmeta.data()),
                       vbmeta.size(), footer.vbmeta_offset));
  AvbVBMetaVerifyResult verify_vbmeta =
      avb_vbmeta_image_verify(vbmeta.data(), vbmeta.size(), nullptr, nullptr);
  switch (verify_vbmeta) {
    case AVB_VBMETA_VERIFY_RESULT_OK:
    case AVB_VBMETA_VERIFY_RESULT_OK_NOT_SIGNED:
      break;
    default:
      return CF_ERRF("Vbmeta verification failed: '{}'",
                     avb_vbmeta_verify_result_to_string(verify_vbmeta));
  }
  // TODO: schuffelen - validate public key from vbmeta

  return AvbParser(std::move(vbmeta));
}

Result<std::string> AvbParser::LookupProperty(std::string_view key) const {
  const char* property = avb_property_lookup(vbmeta_.data(), vbmeta_.size(),
                                             key.data(), key.size(), nullptr);
  CF_EXPECT_NE(property, nullptr);
  return property;
}

AvbParser::AvbParser(std::vector<uint8_t> vbmeta)
    : vbmeta_(std::move(vbmeta)) {}

}  // namespace cuttlefish
