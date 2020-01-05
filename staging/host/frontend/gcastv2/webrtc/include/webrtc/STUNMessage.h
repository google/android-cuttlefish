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
