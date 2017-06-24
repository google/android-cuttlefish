#include "host/ivserver/utils.h"

#include <limits.h>
#include <fstream>

namespace ivserver {

bool RealPath(const std::string &file_name, std::string *real_path) {
  char pathname[PATH_MAX];

  if (realpath(file_name.c_str(), pathname) == nullptr) {
    return false;
  }

  *real_path = std::string(pathname);
  return true;
}

//
// TODO(romitd): Handle errors.
//
bool JsonInit(const std::string &json_file_path, Json::Value *json_root) {
  Json::Reader reader;
  std::ifstream ifs(json_file_path);
  reader.parse(ifs, *json_root);
  return true;
}

}  // namespace ivserver
