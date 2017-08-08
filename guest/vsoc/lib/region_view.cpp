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
#include "common/vsoc/lib/region_view.h"
#include "common/vsoc/lib/region_control.h"

const vsoc_signal_table_layout& vsoc::RegionView::incoming_signal_table() {
  return control_->region_desc().host_to_guest_signal_table;
}

// Returns a pointer to the table that will be used to post signals
const vsoc_signal_table_layout& vsoc::RegionView::outgoing_signal_table() {
  return control_->region_desc().guest_to_host_signal_table;
}
