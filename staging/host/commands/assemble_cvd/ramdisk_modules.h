/*
 * Copyright (C) 2023 The Android Open Source Project
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

#pragma once
#include <set>

static constexpr auto RAMDISK_MODULES = {
    "failover.ko",   "nd_virtio.ko",      "net_failover.ko",
    "virtio_blk.ko", "virtio_console.ko", "virtio_dma_buf.ko",
    "virtio-gpu.ko", "virtio_input.ko",   "virtio_net.ko",
    "virtio_pci.ko", "virtio-rng.ko",     "vmw_vsock_virtio_transport.ko",
};