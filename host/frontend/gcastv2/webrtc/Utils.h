#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::vector<std::string> SplitString(const std::string &s, char c);

std::vector<std::string> SplitString(
        const std::string &s, const std::string &separator);

bool StartsWith(const std::string &s, const std::string &prefix);

void SET_U16(void *_dst, uint16_t x);
void SET_U32(void *_dst, uint32_t x);

uint32_t computeCrc32(const void *_data, size_t size);
