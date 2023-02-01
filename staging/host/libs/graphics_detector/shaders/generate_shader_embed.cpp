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

#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> StrSplit(const std::string& s, const char delimiter) {
  std::vector<std::string> result;
  std::istringstream stream(s);
  std::string item;
  while (std::getline(stream, item, delimiter)) {
    result.push_back(item);
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cout << "Expected 5 arguments.";
    std::exit(1);
  }

  const std::string input_glsl_filename = argv[1];
  const std::string input_spirv_filename = argv[2];
  const std::string input_spirv_varname = argv[3];
  const std::string output_embed_filename = argv[4];

  std::ifstream input_glsl_file(input_glsl_filename);
  if (!input_glsl_file.is_open()) {
    std::cout << "Failed to open input glsl file " << input_spirv_filename;
    std::exit(1);
  }

  const std::string input_glsl(
      (std::istreambuf_iterator<char>(input_glsl_file)),
      std::istreambuf_iterator<char>());
  const std::vector<std::string> input_glsl_lines = StrSplit(input_glsl, '\n');

  std::ifstream input_spirv_file(input_spirv_filename,
                                 std::ios::ate | std::ios::binary);
  if (!input_spirv_file.is_open()) {
    std::cout << "Failed to open input spirv file " << input_spirv_filename;
    std::exit(1);
  }

  const std::size_t input_spirv_bytes_size =
      static_cast<std::size_t>(input_spirv_file.tellg());
  std::vector<unsigned char> input_spirv_bytes(input_spirv_bytes_size);
  input_spirv_file.seekg(0);
  input_spirv_file.read(reinterpret_cast<char*>(input_spirv_bytes.data()),
                        input_spirv_bytes_size);
  input_spirv_file.close();

  std::ofstream output_embed_file(output_embed_filename);
  if (!output_embed_file.is_open()) {
    std::cout << "Failed to open output file " << output_embed_filename;
    std::exit(1);
  }

  output_embed_file << "// Generated from GLSL:\n//\n";
  for (const std::string& input_glsl_line : input_glsl_lines) {
    output_embed_file << "// " << input_glsl_line << "\n";
  }

  output_embed_file << "const std::vector<uint8_t> " << input_spirv_varname
                    << " = {";

  const unsigned char* spirv_data = input_spirv_bytes.data();
  for (std::size_t i = 0; i < input_spirv_bytes_size; i++) {
    constexpr const std::size_t kNumBytesPerLine = 16;

    if (i % kNumBytesPerLine == 0) {
      output_embed_file << "\n\t";
    }

    output_embed_file << "0x" << std::hex << std::setfill('0') << std::setw(2)
                      << static_cast<int>(*spirv_data) << ", ";
    ++spirv_data;
  }
  output_embed_file << "\n};\n\n";

  output_embed_file.close();
  return 0;
}