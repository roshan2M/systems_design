#include "sparse_index.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

// Helper: pad integers ("key_0000" .. "key_0999") so lexicographic order
// matches numeric order in our sorted SSTable file.
static std::string make_key(int i) {
  std::ostringstream oss;
  oss << "key_" << std::setw(4) << std::setfill('0') << i;
  return oss.str();
}

// Seek straight to `range.start_offset` and scan at most one block on disk.
static std::optional<std::string> lookup_in_block(
    const std::filesystem::path& sst_path, const SparseIndex::BlockRange& range,
    const std::string& target_key, std::size_t& records_scanned) {
  std::ifstream in(sst_path, std::ios::binary);
  in.seekg(range.start_offset);

  while ((in >> std::ws) && in.peek() != std::char_traits<char>::eof()) {
    if (range.end_offset && in.tellg() >= *range.end_offset) {
      break;  // Reached start of the next block
    }
    char present;
    std::string key, value;
    if (!(in >> present >> std::quoted(key) >> std::quoted(value))) {
      break;
    }
    ++records_scanned;
    if (key == target_key) {
      return (present == '1') ? std::optional<std::string>(value)
                              : std::nullopt;
    }
    if (key > target_key) {
      break;  // Sorted SSTable: target_key cannot appear past here
    }
  }
  return std::nullopt;
}

// ============================================================================
// Walkthrough Demo
// ============================================================================
int main() {
  const std::filesystem::path sst_path = "/tmp/sparse_index_demo.sst";
  constexpr int N = 1'000;
  constexpr std::size_t STRIDE = 16;  // 1 index entry per 16 records on disk

  SparseIndex index(STRIDE);

  // 1. Write 1,000 sorted keys (even numbers: key_0000, key_0002, ...,
  // key_1998)
  //    and record every 16th key's byte offset in the SparseIndex.
  {
    std::ofstream out(sst_path, std::ios::binary);
    for (int i = 0; i < N; ++i) {
      const std::string key = make_key(i * 2);
      const std::string val = "val_" + std::to_string(i * 2);
      index.add_record(key, out.tellp());
      out << "1 " << std::quoted(key) << ' ' << std::quoted(val) << '\n';
    }
  }

  std::cout << "Built SparseIndex over " << index.total_records()
            << " sorted SSTable records (stride = " << index.stride() << "):\n"
            << "  In-memory index entries : " << index.size() << " (only "
            << std::fixed << std::setprecision(1)
            << (100.0 * index.size() / index.total_records())
            << "% of keys held in RAM!)\n";

  // 2. Out-of-bounds keys are rejected in RAM with ZERO disk reads.
  assert(!index.find_block("key_0000_before").has_value() ||
         !index.find_block("aaa").has_value());
  assert(!index.find_block("zzz").has_value());

  // 3. Look up all 1,000 present keys + 999 missing in-between odd keys,
  //    counting how many records we actually read from disk per query.
  std::size_t total_scanned = 0;
  std::size_t total_queries = 0;

  for (int k = 0; k < 2 * N - 1; ++k) {
    const std::string key = make_key(k);
    auto range = index.find_block(key);
    assert(range.has_value());

    std::size_t scanned = 0;
    auto found = lookup_in_block(sst_path, *range, key, scanned);
    assert(scanned <= STRIDE);  // Never scans more than 1 block!

    if (k % 2 == 0) {
      assert(found == "val_" + std::to_string(k));
    } else {
      assert(found == std::nullopt);
    }

    total_scanned += scanned;
    ++total_queries;
  }

  std::cout << "  Avg disk records scanned: " << std::setprecision(1)
            << (static_cast<double>(total_scanned) / total_queries)
            << " records/query (max " << STRIDE << ", vs " << N
            << " without a sparse index!)\n";

  std::filesystem::remove(sst_path);
  return 0;
}
