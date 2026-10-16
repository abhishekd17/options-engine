// Prints the library version, the compiled SIMD backend and the compiler.
#include "opt/core/version.hpp"

#include <iostream>

int main() {
  std::cout << "options-engine " << opt::version() << '\n'
            << "simd backend: " << opt::simd_backend_name() << " (width " << opt::simd_width()
            << ")\n"
            << "compiler: " << opt::compiler_id() << '\n';
  return 0;
}
