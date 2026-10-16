#pragma once
// Command-line parsing shared by the tools.
//
// Numbers go through std::strtod / std::strtoull (std::from_chars for double
// is not something to rely on in Apple's libc++). A value is accepted only if
// the whole string is consumed, errno is not ERANGE, it is finite, and it is
// inside the caller's [lo, hi] range. Every failure throws UsageError, which
// the tools map to exit code 2.
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace opt::tools {

class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

[[nodiscard]] inline double parse_double(std::string_view flag, std::string_view text, double lo,
                                         double hi) {
  const std::string s{text};
  if (s.empty() || std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    throw UsageError(std::string{flag} + ": expected a number, got '" + s + "'");
  }
  char* end = nullptr;
  errno = 0;
  const double v = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + s.size() || errno == ERANGE || !std::isfinite(v)) {
    throw UsageError(std::string{flag} + ": expected a finite number, got '" + s + "'");
  }
  if (v < lo || v > hi) {
    throw UsageError(std::string{flag} + ": " + s + " is outside [" + std::to_string(lo) + ", " +
                     std::to_string(hi) + "]");
  }
  return v;
}

[[nodiscard]] inline std::uint64_t parse_u64(std::string_view flag, std::string_view text,
                                             std::uint64_t lo, std::uint64_t hi) {
  const std::string s{text};
  // strtoull accepts a leading '-' and wraps; only plain digits are allowed.
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) {
    throw UsageError(std::string{flag} + ": expected a non-negative integer, got '" + s + "'");
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long long v = std::strtoull(s.c_str(), &end, 10);
  if (end != s.c_str() + s.size() || errno == ERANGE) {
    throw UsageError(std::string{flag} + ": integer out of range: '" + s + "'");
  }
  const auto value = static_cast<std::uint64_t>(v);
  if (value < lo || value > hi) {
    throw UsageError(std::string{flag} + ": " + s + " is outside [" + std::to_string(lo) + ", " +
                     std::to_string(hi) + "]");
  }
  return value;
}

// Walks argv[1..]. Typical use:
//   ArgIterator it{argc, argv};
//   while (!it.done()) {
//     const auto flag = it.next();
//     if (flag == "--steps") steps = parse_u64(flag, it.value(flag), 1, 1000);
//     else throw UsageError("unknown flag " + std::string{flag});
//   }
class ArgIterator {
 public:
  ArgIterator(int argc, const char* const* argv)
      : args_(argv + 1, argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U) {}
  explicit ArgIterator(std::span<const char* const> args) : args_(args) {}

  [[nodiscard]] bool done() const noexcept { return pos_ >= args_.size(); }

  // The next argument (a flag). Precondition: !done().
  [[nodiscard]] std::string_view next() { return args_[pos_++]; }

  // The value that follows `flag`; throws UsageError if there is none.
  [[nodiscard]] std::string_view value(std::string_view flag) {
    if (done()) {
      throw UsageError(std::string{flag} + ": missing value");
    }
    return args_[pos_++];
  }

 private:
  std::span<const char* const> args_;
  std::size_t pos_ = 0;
};

}  // namespace opt::tools
