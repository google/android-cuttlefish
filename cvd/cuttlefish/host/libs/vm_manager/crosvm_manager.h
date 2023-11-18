#pragma once

#include <string>

namespace cuttlefish {
namespace vm_manager {

struct CrosvmManager {
  static std::string name() {
    return "crosvm";
  }
};

}
}
