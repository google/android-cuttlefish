/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "common/libs/net/netlink_client.h"

#include <linux/rtnetlink.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>
#include <memory>

using ::testing::ElementsAreArray;
using ::testing::MatchResultListener;
using ::testing::Return;

namespace avd {
namespace {
extern "C" void klog_write(int /* level */, const char* /* format */, ...) {}

// Dump hex buffer to test log.
void Dump(MatchResultListener* result_listener, const char* title,
          const uint8_t* data, size_t length) {
  for (size_t item = 0; item < length;) {
    *result_listener << title;
    do {
      result_listener->stream()->width(2);
      result_listener->stream()->fill('0');
      *result_listener << std::hex << +data[item] << " ";
      ++item;
    } while (item & 0xf);
    *result_listener << "\n";
  }
}

// Compare two memory areas byte by byte, print information about first
// difference. Dumps both bufferst to user log.
bool Compare(MatchResultListener* result_listener,
             const uint8_t* exp, const uint8_t* act, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    if (exp[index] != act[index]) {
      *result_listener << "\nUnexpected data at offset " << index << "\n";
      Dump(result_listener, "Data Expected: ", exp, length);
      Dump(result_listener, "  Data Actual: ", act, length);
      return false;
    }
  }

  return true;
}

// Matcher validating Netlink Request data.
MATCHER_P2(RequestDataIs, data, length, "Matches expected request data") {
  size_t offset = sizeof(nlmsghdr);
  if (offset + length != arg->RequestLength()) {
    *result_listener << "Unexpected request length: "
                     << arg->RequestLength() - offset << " vs " << length;
    return false;
  }

  // Note: Request begins with header (nlmsghdr). Header is not covered by this
  // call.
  const uint8_t* exp_data = static_cast<const uint8_t*>(
      static_cast<const void*>(data));
  const uint8_t* act_data = static_cast<const uint8_t*>(arg->RequestData());
  return Compare(
      result_listener, exp_data, &act_data[offset], length);
}

MATCHER_P4(RequestHeaderIs, length, type, flags, seq,
           "Matches request header") {
  nlmsghdr* header = static_cast<nlmsghdr*>(arg->RequestData());
  if (arg->RequestLength() < sizeof(header)) {
    *result_listener << "Malformed header: too short.";
    return false;
  }

  if (header->nlmsg_len != length) {
    *result_listener << "Invalid message length: "
                     << header->nlmsg_len << " vs " << length;
    return false;
  }

  if (header->nlmsg_type != type) {
    *result_listener << "Invalid header type: "
                     << header->nlmsg_type << " vs " << type;
    return false;
  }

  if (header->nlmsg_flags != flags) {
    *result_listener << "Invalid header flags: "
                     << header->nlmsg_flags << " vs " << flags;
    return false;
  }

  if (header->nlmsg_seq != seq) {
    *result_listener << "Invalid header sequence number: "
                     << header->nlmsg_seq << " vs " << seq;
    return false;
  }

  return true;
}
}  // namespace

class NetlinkClientTest : public ::testing::Test {
 protected:
  std::unique_ptr<NetlinkClient> nl_client_;
};

TEST_F(NetlinkClientTest, BasicStringNode) {
  constexpr uint16_t kDummyTag = 0xfce2;
  constexpr char kLongString[] = "long string";

  struct {
    // 11 bytes of text + padding 0 + 4 bytes of header.
    const uint16_t attr_length = 0x10;
    const uint16_t attr_type = kDummyTag;
    char text[sizeof(kLongString)];  // sizeof includes padding 0.
  } expected;

  memcpy(&expected.text, kLongString, sizeof(kLongString));

  auto request =
      avd::NetlinkRequest::New(RTM_SETLINK, 0);
  request->AddString(kDummyTag, kLongString);
  EXPECT_THAT(request, RequestDataIs(&expected, sizeof(expected)));
}

TEST_F(NetlinkClientTest, BasicIntNode) {
  // Basic { Dummy: Value } test.
  constexpr uint16_t kDummyTag = 0xfce2;
  constexpr int32_t kValue = 0x1badd00d;

  struct {
    const uint16_t attr_length = 0x8;  // 4 bytes of value + 4 bytes of header.
    const uint16_t attr_type = kDummyTag;
    const uint32_t attr_value = kValue;
  } expected;

  auto request =
      avd::NetlinkRequest::New(RTM_SETLINK, 0);
  request->AddInt32(kDummyTag, kValue);
  EXPECT_THAT(request, RequestDataIs(&expected, sizeof(expected)));
}

TEST_F(NetlinkClientTest, SingleList) {
  // List: { Dummy: Value}
  constexpr uint16_t kDummyTag = 0xfce2;
  constexpr uint16_t kListTag = 0xcafe;
  constexpr int32_t kValue = 0x1badd00d;

  struct {
    const uint16_t list_length = 0xc;
    const uint16_t list_type = kListTag;
    const uint16_t attr_length = 0x8;  // 4 bytes of value + 4 bytes of header.
    const uint16_t attr_type = kDummyTag;
    const uint32_t attr_value = kValue;
  } expected;

  auto request =
      avd::NetlinkRequest::New(RTM_SETLINK, 0);
  request->PushList(kListTag);
  request->AddInt32(kDummyTag, kValue);
  request->PopList();

  EXPECT_THAT(request, RequestDataIs(&expected, sizeof(expected)));
}

TEST_F(NetlinkClientTest, NestedList) {
  // List1: { List2: { Dummy: Value}}
  constexpr uint16_t kDummyTag = 0xfce2;
  constexpr uint16_t kList1Tag = 0xcafe;
  constexpr uint16_t kList2Tag = 0xfeed;
  constexpr int32_t kValue = 0x1badd00d;

  struct {
    const uint16_t list1_length = 0x10;
    const uint16_t list1_type = kList1Tag;
    const uint16_t list2_length = 0xc;
    const uint16_t list2_type = kList2Tag;
    const uint16_t attr_length = 0x8;
    const uint16_t attr_type = kDummyTag;
    const uint32_t attr_value = kValue;
  } expected;

  auto request =
      avd::NetlinkRequest::New(RTM_SETLINK, 0);
  request->PushList(kList1Tag);
  request->PushList(kList2Tag);
  request->AddInt32(kDummyTag, kValue);
  request->PopList();
  request->PopList();

  EXPECT_THAT(request, RequestDataIs(&expected, sizeof(expected)));
}

TEST_F(NetlinkClientTest, ListSequence) {
  // List1: { Dummy1: Value1}, List2: { Dummy2: Value2 }
  constexpr uint16_t kDummy1Tag = 0xfce2;
  constexpr uint16_t kDummy2Tag = 0xfd38;
  constexpr uint16_t kList1Tag = 0xcafe;
  constexpr uint16_t kList2Tag = 0xfeed;
  constexpr int32_t kValue1 = 0x1badd00d;
  constexpr int32_t kValue2 = 0xfee1;

  struct {
    const uint16_t list1_length = 0xc;
    const uint16_t list1_type = kList1Tag;
    const uint16_t attr1_length = 0x8;
    const uint16_t attr1_type = kDummy1Tag;
    const uint32_t attr1_value = kValue1;
    const uint16_t list2_length = 0xc;
    const uint16_t list2_type = kList2Tag;
    const uint16_t attr2_length = 0x8;
    const uint16_t attr2_type = kDummy2Tag;
    const uint32_t attr2_value = kValue2;
  } expected;

  auto request =
      avd::NetlinkRequest::New(RTM_SETLINK, 0);
  request->PushList(kList1Tag);
  request->AddInt32(kDummy1Tag, kValue1);
  request->PopList();
  request->PushList(kList2Tag);
  request->AddInt32(kDummy2Tag, kValue2);
  request->PopList();

  EXPECT_THAT(request, RequestDataIs(&expected, sizeof(expected)));
}

TEST_F(NetlinkClientTest, ComplexList) {
  // List1: { List2: { Dummy1: Value1 }, Dummy2: Value2 }
  constexpr uint16_t kDummy1Tag = 0xfce2;
  constexpr uint16_t kDummy2Tag = 0xfd38;
  constexpr uint16_t kList1Tag = 0xcafe;
  constexpr uint16_t kList2Tag = 0xfeed;
  constexpr int32_t kValue1 = 0x1badd00d;
  constexpr int32_t kValue2 = 0xfee1;

  struct {
    const uint16_t list1_length = 0x18;
    const uint16_t list1_type = kList1Tag;
    const uint16_t list2_length = 0xc;  // Note, this only covers until kValue1.
    const uint16_t list2_type = kList2Tag;
    const uint16_t attr1_length = 0x8;
    const uint16_t attr1_type = kDummy1Tag;
    const uint32_t attr1_value = kValue1;
    const uint16_t attr2_length = 0x8;
    const uint16_t attr2_type = kDummy2Tag;
    const uint32_t attr2_value = kValue2;
  } expected;

  auto request =
      avd::NetlinkRequest::New(RTM_SETLINK, 0);
  request->PushList(kList1Tag);
  request->PushList(kList2Tag);
  request->AddInt32(kDummy1Tag, kValue1);
  request->PopList();
  request->AddInt32(kDummy2Tag, kValue2);
  request->PopList();

  EXPECT_THAT(request, RequestDataIs(&expected, sizeof(expected)));
}

TEST_F(NetlinkClientTest, SimpleNetlinkCreateHeader) {
  auto request =
      avd::NetlinkRequest::New(RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL);
  constexpr char kValue[] = "random string";
  request->AddString(0, kValue);  // Have something to work with.

  constexpr size_t kMsgLength =
      sizeof(nlmsghdr) + sizeof(nlattr) + RTA_ALIGN(sizeof(kValue));

  EXPECT_THAT(request, RequestHeaderIs(
      kMsgLength,  // Expected size of message.
      RTM_NEWLINK,  // Results from creane_new_iface=true in CreateRequest.
      NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL | NLM_F_REQUEST,  // Ditto.
      0u));
}

TEST_F(NetlinkClientTest, SimpleNetlinkUpdateHeader) {
  auto request =
      avd::NetlinkRequest::New(RTM_SETLINK, 0);
  constexpr char kValue[] = "random string";
  request->AddString(0, kValue);  // Have something to work with.

  constexpr size_t kMsgLength =
      sizeof(nlmsghdr) + sizeof(nlattr) + RTA_ALIGN(sizeof(kValue));

  EXPECT_THAT(request, RequestHeaderIs(
      kMsgLength,  // Expected size of message.
      RTM_SETLINK,  // Results from creane_new_iface=true in CreateRequest.
      NLM_F_REQUEST | NLM_F_ACK,  // Ditto.
      0u));
}

}  // namespace avd
