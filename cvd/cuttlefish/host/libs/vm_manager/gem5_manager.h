#pragma once

#include <string>

namespace cuttlefish {
namespace vm_manager {

struct Gem5Manager {
  static std::string name() {
    return "gem5";
  }
};

}
}
