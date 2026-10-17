#include "opt/deribit/snapshot.hpp"
#include "opt/pricing/types.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
namespace opt::deribit {
namespace {
using nlohmann::json;
namespace fs = std::filesystem;
json read_json(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    throw DataError(file.string() + ": cannot open file");
  }
  try {
    return json::parse(in);
  } catch (const json::exception& e) {
    throw DataError(file.string() + ": invalid JSON: " + e.what());
  }
}
const json& result_array(const json& doc, const fs::path& file) {
  if (!doc.is_object() || !doc.contains("result") || !doc["result"].is_array()) {
    throw DataError(file.string() + ": /result: missing or not an array");
  }
  const json& r = doc["result"];
  if (r.empty()) {
    throw DataError(file.string() + ": /result: empty");
  }
  return r;
}
// Per-row field access. A missing field or wrong type returns false and the
// caller counts "bad_field:<name>".
bool get_string(const json& row, const char* name, std::string& out) {
  const auto it = row.find(name);
  if (it == row.end() || !it->is_string()) {
    return false;
  }
  out = it->get<std::string>();
  return true;
}
bool get_number(const json& row, const char* name, double& out) {
  const auto it = row.find(name);
  if (it == row.end() || !it->is_number()) {
    return false;
  }
  out = it->get<double>();
  return true;
}
bool get_int(const json& row, const char* name, std::int64_t& out) {
  const auto it = row.find(name);
  if (it == row.end() || !it->is_number_integer()) {
    return false;
  }
  out = it->get<std::int64_t>();
  return true;
}
// Number or JSON null (-> nullopt). Missing or other types return false.
bool get_nullable(const json& row, const char* name, std::optional<double>& out) {
  const auto it = row.find(name);
  if (it == row.end()) {
    return false;
  }
  if (it->is_null()) {
    out.reset();
    return true;
  }
  if (!it->is_number()) {
    return false;
  }
  out = it->get<double>();
  return true;
}
bool positive_finite(double x) noexcept { return x > 0.0 && std::isfinite(x); }
struct Instrument {
  std::string kind;
  OptionType type = OptionType::Call;
  double strike = 0.0;
  std::int64_t expiry_ms = 0;
  bool used = false;
};
struct Row {
  Quote q;
  std::int64_t created_ms = 0;
};
}  // namespace
Snapshot load_snapshot(const fs::path& dir) {
  const fs::path f_inst = dir / "instruments.json";
  const fs::path f_book = dir / "book_summary.json";
  const fs::path f_index = dir / "index_price.json";
  const fs::path f_meta = dir / "meta.json";
  const json inst_doc = read_json(f_inst);
  const json book_doc = read_json(f_book);
  const json index_doc = read_json(f_index);
  const json meta_doc = read_json(f_meta);
  Snapshot snap;
  auto skip = [&snap](const std::string& reason) { ++snap.skipped[reason]; };
  if (!meta_doc.is_object() || !meta_doc.contains("currency") ||
      !meta_doc["currency"].is_string()) {
    throw DataError(f_meta.string() + ": /currency: missing or not a string");
  }
  snap.currency = meta_doc["currency"].get<std::string>();
  {
    const auto r = index_doc.find("result");
    if (r == index_doc.end() || !r->is_object() || !r->contains("index_price") ||
        !(*r)["index_price"].is_number()) {
      throw DataError(f_index.string() + ": /result/index_price: missing or not a number");
    }
    snap.index_price = (*r)["index_price"].get<double>();
    if (!positive_finite(snap.index_price)) {
      throw DataError(f_index.string() + ": /result/index_price: not positive");
    }
  }
  // Instruments by name.
  std::unordered_map<std::string, Instrument> instruments;
  for (const json& row : result_array(inst_doc, f_inst)) {
    std::string name;
    std::string type;
    Instrument in;
    if (!row.is_object() || !get_string(row, "instrument_name", name)) {
      skip("bad_field:instrument_name");
      continue;
    }
    if (!get_string(row, "kind", in.kind)) {
      skip("bad_field:kind");
      continue;
    }
    if (in.kind != "option") {
      skip("not_option");
      continue;
    }
    if (!get_string(row, "option_type", type)) {
      skip("bad_field:option_type");
      continue;
    }
    if (type != "call" && type != "put") {
      skip("bad_value:option_type");
      continue;
    }
    in.type = type == "call" ? OptionType::Call : OptionType::Put;
    if (!get_number(row, "strike", in.strike)) {
      skip("bad_field:strike");
      continue;
    }
    if (!positive_finite(in.strike)) {
      skip("bad_value:strike");
      continue;
    }
    if (!get_int(row, "expiration_timestamp", in.expiry_ms)) {
      skip("bad_field:expiration_timestamp");
      continue;
    }
    instruments.emplace(std::move(name), std::move(in));
  }
  // Book rows joined with instruments.
  std::vector<Row> rows;
  for (const json& row : result_array(book_doc, f_book)) {
    Row r;
    Quote& q = r.q;
    if (!row.is_object() || !get_string(row, "instrument_name", q.name)) {
      skip("bad_field:instrument_name");
      continue;
    }
    const auto it = instruments.find(q.name);
    if (it == instruments.end()) {
      skip("unmatched");
      continue;
    }
    it->second.used = true;
    q.type = it->second.type;
    q.strike = it->second.strike;
    q.expiry_ms = it->second.expiry_ms;
    if (!get_nullable(row, "bid_price", q.bid_btc)) {
      skip("bad_field:bid_price");
      continue;
    }
    if (!get_nullable(row, "ask_price", q.ask_btc)) {
      skip("bad_field:ask_price");
      continue;
    }
    if (!get_number(row, "mark_price", q.mark_btc)) {
      skip("bad_field:mark_price");
      continue;
    }
    if (!get_number(row, "mark_iv", q.mark_iv)) {
      skip("bad_field:mark_iv");
      continue;
    }
    if (!get_number(row, "underlying_price", q.underlying_price)) {
      skip("bad_field:underlying_price");
      continue;
    }
    if (!get_int(row, "creation_timestamp", r.created_ms)) {
      skip("bad_field:creation_timestamp");
      continue;
    }
    if (!get_number(row, "open_interest", q.open_interest)) {
      skip("bad_field:open_interest");
      continue;
    }
    if (!positive_finite(q.underlying_price)) {
      skip("bad_value:underlying_price");
      continue;
    }
    if (!(q.mark_btc >= 0.0)) {
      skip("bad_value:mark_price");
      continue;
    }
    if (!(q.mark_iv > 0.0)) {
      skip("bad_value:mark_iv");
      continue;
    }
    q.mark_iv /= 100.0;
    if (q.bid_btc && q.ask_btc && *q.bid_btc > *q.ask_btc) {
      skip("crossed_quote");
      q.bid_btc.reset();
      q.ask_btc.reset();
    }
    rows.push_back(std::move(r));
  }
  for (const auto& [name, in] : instruments) {
    if (!in.used) {
      skip("unmatched");
    }
  }
  if (rows.empty()) {
    throw DataError(f_book.string() + ": /result: no valid option rows");
  }
  snap.valuation_ms = rows.front().created_ms;
  snap.valuation_min_ms = rows.front().created_ms;
  for (const Row& r : rows) {
    snap.valuation_ms = std::max(snap.valuation_ms, r.created_ms);
    snap.valuation_min_ms = std::min(snap.valuation_min_ms, r.created_ms);
  }
  // Expiry filter, then group by expiry.
  std::map<std::int64_t, std::vector<std::size_t>> by_expiry;
  for (Row& r : rows) {
    if (r.q.expiry_ms <= snap.valuation_ms) {
      skip("expired");
      continue;
    }
    r.q.tau = static_cast<double>(r.q.expiry_ms - snap.valuation_ms) / kMsPerYear;
    by_expiry[r.q.expiry_ms].push_back(snap.quotes.size());
    snap.quotes.push_back(std::move(r.q));
  }
  if (snap.quotes.empty()) {
    throw DataError(f_book.string() + ": /result: every option row is expired");
  }
  for (auto& [expiry_ms, idx] : by_expiry) {
    std::vector<double> und;
    und.reserve(idx.size());
    for (const std::size_t i : idx) {
      und.push_back(snap.quotes[i].underlying_price);
    }
    std::sort(und.begin(), und.end());
    const std::size_t n = und.size();
    const double fwd = n % 2 == 1 ? und[n / 2] : 0.5 * (und[n / 2 - 1] + und[n / 2]);
    for (const std::size_t i : idx) {
      Quote& q = snap.quotes[i];
      if (std::abs(q.underlying_price - fwd) > 1e-9 * fwd) {
        skip("forward_mismatch");
      }
      q.forward = fwd;
    }
    std::sort(idx.begin(), idx.end(), [&snap](std::size_t l, std::size_t r) {
      const Quote& a = snap.quotes[l];
      const Quote& b = snap.quotes[r];
      if (a.strike != b.strike) {
        return a.strike < b.strike;
      }
      return a.type < b.type;
    });
    ExpirySlice s;
    s.expiry_ms = expiry_ms;
    s.tau = snap.quotes[idx.front()].tau;
    s.forward = fwd;
    s.quote_index = std::move(idx);
    snap.expiries.push_back(std::move(s));
  }
  return snap;
}
std::vector<fs::path> list_series(const fs::path& dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    throw DataError(dir.string() + ": not a directory");
  }
  static const std::regex kName{"^[A-Z]+-[0-9]{8}T[0-9]{6}Z$"};
  std::vector<fs::path> out;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.is_directory() && std::regex_match(entry.path().filename().string(), kName)) {
      out.push_back(entry.path());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}
}  // namespace opt::deribit
