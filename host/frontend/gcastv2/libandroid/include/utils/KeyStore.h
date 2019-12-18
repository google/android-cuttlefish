#pragma once

#include <cstdint>
#include <string>
#include <vector>

void setCertificateOrKey(
        const std::string &name, const void *_data, size_t size);

bool getCertificateOrKey(
        const std::string &name, std::vector<uint8_t> *data);

