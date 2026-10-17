#pragma once
// Locates the committed Deribit test data (tests/data/deribit).
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
namespace opt::test {
inline std::filesystem::path deribit_data_dir() {
  return std::filesystem::path{OPTENG_TEST_DATA_DIR} / "deribit";
}
// The single full-chain snapshot BTC-* directly under tests/data/deribit.
inline std::filesystem::path committed_snapshot_dir() {
  std::vector<std::filesystem::path> found;
  for (const auto& e : std::filesystem::directory_iterator(deribit_data_dir())) {
    if (e.is_directory() && e.path().filename().string().rfind("BTC-", 0) == 0) {
      found.push_back(e.path());
    }
  }
  if (found.size() != 1) {
    throw std::runtime_error("expected exactly one BTC-* snapshot in " +
                             deribit_data_dir().string());
  }
  return found.front();
}
}  // namespace opt::test
