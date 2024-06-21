#include <iostream>
#include <string>
#include <vector>

int main() {
  // Exercise dynamic memory allocation
  std::vector<std::string> test_vec;
  for (size_t i = 0; i < 100; i++) {
    test_vec.emplace_back(std::to_string(i));
  }
  // Exercise writing to stderr
  std::cout << "Allocated vector with " << test_vec.size() << " members\n";
}
