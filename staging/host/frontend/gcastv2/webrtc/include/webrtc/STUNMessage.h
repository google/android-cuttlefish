/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

struct STUNMessage {
    explicit STUNMessage(uint16_t type, const uint8_t transactionID[12]);
    explicit STUNMessage(const void *data, size_t size);

    bool isValid() const;

    uint16_t type() const;

    void addAttribute(uint16_t type) {
        addAttribute(type, nullptr, 0);
    }

    void addAttribute(uint16_t type, const void *data, size_t size);
    void addMessageIntegrityAttribute(std::string_view password);
    void addFingerprint();

    bool findAttribute(uint16_t type, const void **data, size_t *size) const;

    const uint8_t *data();
    size_t size() const;

    void dump(std::optional<std::string_view> password = std::nullopt) const;

private:
    bool mIsValid;
    std::vector<uint8_t> mData;
    bool mAddedMessageIntegrity;

    void validate();

    bool verifyMessageIntegrity(size_t offset, std::string_view password) const;
    bool verifyFingerprint(size_t offset) const;
};
