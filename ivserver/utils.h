#pragma once

#include <json/json.h>
#include <string>

namespace ivserver {

//
// Gets the canonicalized absolute name like REALPATH(1).
// Returns true on success, false otherwise.
//
extern bool RealPath(const std::string &file_name, std::string *real_path);

//
// Initializes a Json::Value object from a json file.
// Returns true on success, false otherwise.
//
extern bool JsonInit(const std::string &json_file_path, Json::Value *json_root);

}  // namespace ivserver
