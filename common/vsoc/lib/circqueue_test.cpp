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
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>

#include "common/vsoc/lib/circqueue_impl.h"
#include "common/vsoc/lib/mock_region_view.h"

#define EXPECT_BLOCK(region, tid) \
    EXPECT_TRUE(region.IsBlocking(tid))

namespace {

constexpr int kQueueSizeLog2 = 16;
constexpr int kQueueCapacity = 1 << kQueueSizeLog2;
constexpr int kMaxPacketSize = 1024;

constexpr int kNumReadingThread = 5;
constexpr int kNumWritingThread = 5;

struct CircQueueTestRegionLayout : public vsoc::layout::RegionLayout {
  vsoc::layout::CircularByteQueue<kQueueSizeLog2> byte_queue;
  vsoc::layout::CircularPacketQueue<kQueueSizeLog2, kMaxPacketSize> packet_queue;
};

typedef vsoc::test::MockRegionView<CircQueueTestRegionLayout> CircQueueRegionView;

class CircQueueTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    region_.Open();
  }

  CircQueueRegionView region_;
};

intptr_t ReadBytes(CircQueueRegionView* region, int bytes) {
  char buffer_out[bytes];
  CircQueueTestRegionLayout* layout = region->data();
  return layout->byte_queue.Read(region, buffer_out, bytes);
}

intptr_t WriteBytes(CircQueueRegionView* region, int bytes) {
  char buffer_in[bytes];
  CircQueueTestRegionLayout* layout = region->data();
  return layout->byte_queue.Write(region, buffer_in, bytes);
}

intptr_t ReadPacket(CircQueueRegionView* region, int max_size) {
  char buffer_out[max_size];
  CircQueueTestRegionLayout* layout = region->data();
  return layout->packet_queue.Read(region, buffer_out, max_size);
}

intptr_t WritePacket(CircQueueRegionView* region, int packet_size) {
  char buffer_in[packet_size];
  CircQueueTestRegionLayout* layout = region->data();
  return layout->packet_queue.Write(region, buffer_in, packet_size);
}

void ReadBytesInChunk(CircQueueRegionView* region, int total_size, int chuck_size) {
  char buffer_out[chuck_size];
  CircQueueTestRegionLayout* layout = region->data();
  int total_read = 0;
  int remaining = total_size;
  while (remaining >= chuck_size) {
    int ret = layout->byte_queue.Read(region, buffer_out, chuck_size);
    total_read += ret;
    remaining -= ret;
  }
  if (remaining > 0) {
    total_read += layout->byte_queue.Write(region, buffer_out, remaining);
  }
  EXPECT_EQ(total_read, total_size);
}

void WriteBytesInChunk(CircQueueRegionView* region, int total_size, int chuck_size) {
  char buffer_in[chuck_size];
  CircQueueTestRegionLayout* layout = region->data();
  int total_write = 0;
  int remaining = total_size;
  while (remaining >= chuck_size) {
    int ret = layout->byte_queue.Write(region, buffer_in, chuck_size);
    total_write += ret;
    remaining -= ret;
  }
  if (remaining > 0) {
    total_write += layout->byte_queue.Write(region, buffer_in, remaining);
  }
  EXPECT_EQ(total_write, total_size);
}

void ReadManyPackets(CircQueueRegionView* region, int num_packets, int packet_size) {
  char buffer_out[packet_size];
  CircQueueTestRegionLayout* layout = region->data();
  int total_read = 0;
  int remaining = num_packets;
  while (remaining > 0) {
    int ret = layout->packet_queue.Read(region, buffer_out, packet_size);
    total_read += ret;
    remaining--;
  }
  EXPECT_EQ(total_read, num_packets * packet_size);
}

void WriteManyPackets(CircQueueRegionView* region, int num_packets, int packet_size) {
  char buffer_in[packet_size];
  CircQueueTestRegionLayout* layout = region->data();
  int total_write = 0;
  int remaining = num_packets;
  while (remaining > 0) {
    int ret = layout->packet_queue.Write(region, buffer_in, packet_size);
    total_write += ret;
    remaining--;
  }
  EXPECT_EQ(total_write, num_packets * packet_size);
}

// ByteQueue Tests

// Test writing bytes
TEST_F(CircQueueTest, ByteQueueSimpleWrite) {
  const int num_bytes = 8;
  EXPECT_EQ(num_bytes, WriteBytes(&this->region_, num_bytes));
}

// Test reading bytes
TEST_F(CircQueueTest, ByteQueueSimpleRead) {
  const int num_bytes = 8;
  EXPECT_EQ(num_bytes, WriteBytes(&this->region_, num_bytes));
  EXPECT_EQ(num_bytes, ReadBytes(&this->region_, num_bytes));
}

// Test reading on an empty queue. Expect blocking.
TEST_F(CircQueueTest, ByteQueueReadOnEmpty) {
  const int num_bytes = 8;

  // Spawn a thread to read from queue. Expect it to block.
  std::thread reading_thread(ReadBytes, &this->region_, num_bytes);
  EXPECT_BLOCK(region_, reading_thread.get_id());

  // Write expected bytes in so that we can clean up properly.
  std::thread writing_thread(WriteBytes, &this->region_, num_bytes);
  writing_thread.join();

  reading_thread.join();
}

// Test writing on a full queue. Expect blocking.
TEST_F(CircQueueTest, ByteQueueWriteOnFull) {
  // Fill the queue.
  const int capacity_bytes = kQueueCapacity;
  EXPECT_EQ(capacity_bytes, WriteBytes(&this->region_, capacity_bytes));

  // Now the queue is full, any further write would block.
  const int num_bytes = 8;
  std::thread writing_thread(WriteBytes, &this->region_, num_bytes);
  EXPECT_BLOCK(region_, writing_thread.get_id());

  // Read the extra bytes out so that we can clean up properly.
  std::thread reading_thread(ReadBytes, &this->region_, num_bytes);
  reading_thread.join();

  writing_thread.join();
}

// Test if bytes being read out are the same as ones being written in.
TEST_F(CircQueueTest, ByteQueueContentIntegrity) {
  const int num_bytes = 8;
  CircQueueTestRegionLayout* layout = this->region_.data();

  char buffer_in[num_bytes] = {'a'};
  layout->byte_queue.Write(&this->region_, buffer_in, num_bytes);

  char buffer_out[num_bytes] = {'b'};
  layout->byte_queue.Read(&this->region_, buffer_out, num_bytes);

  for (int i=0; i<num_bytes; i++) {
    EXPECT_EQ(buffer_in[i], buffer_out[i]);
  }
}

// Test writing more bytes than capacity
TEST_F(CircQueueTest, ByteQueueWriteTooManyBytes) {
  const int extra_bytes = 8;
  const int num_bytes = kQueueCapacity + extra_bytes;
  EXPECT_EQ(-ENOSPC, WriteBytes(&this->region_, num_bytes));
}

// Test multiple bytes read/write
TEST_F(CircQueueTest, ByteQueueMultipleReadWrite) {
  const int chunk_size = 7;
  const int total_size = 3.3 * kQueueCapacity;
  std::vector<std::thread> reading_threads;
  std::vector<std::thread> writing_threads;
  for (int i=0; i<kNumReadingThread; i++) {
    reading_threads.emplace_back(
        std::thread(ReadBytesInChunk, &this->region_, total_size, chunk_size));
  }
  for (int i=0; i<kNumWritingThread; i++) {
    writing_threads.emplace_back(
        std::thread(WriteBytesInChunk, &this->region_, total_size, chunk_size));
  }
  std::for_each(reading_threads.begin(), reading_threads.end(), [](std::thread& t) { t.join(); });
  std::for_each(writing_threads.begin(), writing_threads.end(), [](std::thread& t) { t.join(); });
}

// PacketQueue Tests

// Test writing packet
TEST_F(CircQueueTest, PacketQueueSimpleWrite) {
  const int packet_size = 8;
  EXPECT_EQ(packet_size, WritePacket(&this->region_, packet_size));
}

// Test reading packet
TEST_F(CircQueueTest, PacketQueueSimpleRead) {
  const int packet_size = 8;
  EXPECT_EQ(packet_size, WritePacket(&this->region_, packet_size));
  EXPECT_EQ(packet_size, ReadPacket(&this->region_, packet_size));
}

// Test reading on an empty queue. Expect blocking.
TEST_F(CircQueueTest, PacketQueueReadOnEmpty) {
  const int packet_size = 8;

  // Spawn a thread to read from queue. Expect it to block.
  std::thread reading_thread(ReadPacket, &this->region_, packet_size);
  EXPECT_BLOCK(region_, reading_thread.get_id());

  // Write expected bytes in so that we can clean up properly.
  std::thread writing_thread(WritePacket, &this->region_, packet_size);
  writing_thread.join();

  reading_thread.join();
}

// Test writing on a full queue. Expect blocking.
TEST_F(CircQueueTest, PacketQueueWriteOnFull) {
  // Fill the queue.
  const int packet_size = kMaxPacketSize;
  int capacity_bytes = kQueueCapacity;
  while (capacity_bytes >= packet_size) {
    EXPECT_EQ(packet_size, WritePacket(&this->region_, packet_size));
    capacity_bytes -= (packet_size + sizeof(uint32_t));
  }

  // Now the queue is full, any further write would block.
  std::thread writing_thread(WritePacket, &this->region_, packet_size);
  EXPECT_BLOCK(region_, writing_thread.get_id());

  // Read the extra bytes out so that we can clean up properly.
  std::thread reading_thread(ReadPacket, &this->region_, packet_size);
  reading_thread.join();

  writing_thread.join();
}

// Test if packet being read out are the same as one being written in.
TEST_F(CircQueueTest, PacketQueueContentIntegrity) {
  const int packet_size = 8;
  CircQueueTestRegionLayout* layout = this->region_.data();

  char buffer_in[packet_size] = {'a'};
  layout->packet_queue.Write(&this->region_, buffer_in, packet_size);

  char buffer_out[packet_size] = {'b'};
  layout->packet_queue.Read(&this->region_, buffer_out, packet_size);

  for (int i=0; i<packet_size; i++) {
    EXPECT_EQ(buffer_in[i], buffer_out[i]);
  }
}

// Test writing packet larger than capacity
TEST_F(CircQueueTest, PacketQueueWriteTooLargePacket) {
  const int extra_bytes = 8;
  const int packet_size = kQueueCapacity + extra_bytes;
  EXPECT_EQ(-ENOSPC, WritePacket(&this->region_, packet_size));
}

// Test reading packet larger than can handle
TEST_F(CircQueueTest, PacketQueueReadTooLargePacket) {
  const int extra_bytes = 8;
  const int small_packet = 8;
  const int large_packet = small_packet + extra_bytes;

  WritePacket(&this->region_, large_packet);
  char buffer_out[small_packet];
  CircQueueTestRegionLayout* layout = this->region_.data();
  EXPECT_EQ(-ENOSPC, layout->packet_queue.Read(&this->region_, buffer_out, small_packet));
}

// Test multiple packets read/write
TEST_F(CircQueueTest, PacketQueueMultipleReadWrite) {
  const int packet_size = kMaxPacketSize;
  const int num_packets = 1.5 * (kQueueCapacity / packet_size);
  std::vector<std::thread> reading_threads;
  std::vector<std::thread> writing_threads;
  for (int i=0; i<kNumReadingThread; i++) {
    reading_threads.emplace_back(
        std::thread(ReadManyPackets, &this->region_, num_packets, packet_size));
  }
  for (int i=0; i<kNumWritingThread; i++) {
    writing_threads.emplace_back(
        std::thread(WriteManyPackets, &this->region_, num_packets, packet_size));
  }
  std::for_each(reading_threads.begin(), reading_threads.end(), [](std::thread& t) { t.join(); });
  std::for_each(writing_threads.begin(), writing_threads.end(), [](std::thread& t) { t.join(); });
}

}  // namespace
