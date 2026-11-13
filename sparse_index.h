#pragma once

#include <algorithm>
#include <cstddef>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// Educational Sparse Index (for Sorted SSTable Files)
// ============================================================================
//
// Because an SSTable file on disk is immutable and sorted by key, we do NOT
// need an in-memory entry for every single key (a "dense" index). Instead, we
// keep a "sparse" index storing the byte offset of every N-th key (1 per block):
//
//   Sparse Index (RAM)               Sorted SSTable File (Disk)
//   ┌─────────┬────────┐             ┌──────────────────────────┐
//   │ "alice" │ off  0 │ ──────────► │ 0:   1 "alice"   "100"   │ ┐
//   │ "dave"  │ off 58 │ ──────┐     │ 19:  1 "bob"     "200"   │ │ Block 0
//   │ "grace" │ off 115│ ────┐ │     │ 38:  1 "charlie" "300"   │ ┘
//   └─────────┴────────┘     │ └────►│ 58:  1 "dave"    "400"   │ ┐
//                            │       │ 77:  1 "erin"    "500"   │ │ Block 1
//                            │       │ 96:  1 "frank"   "600"   │ ┘
//                            └──────►│ 115: 1 "grace"   "700"   │ ── Block 2
//                                    └──────────────────────────┘
//
// Lookup for `key` (e.g., "erin"):
//   1. Key-bounds check: if `key < min_key` or `key > max_key`, return nullopt
//      immediately without touching disk!
//   2. Binary search (`std::upper_bound`) the sparse index in O(log B) RAM time
//      to find the last block header <= "erin" ("dave" @ byte 58) and the next
//      block boundary ("grace" @ byte 115).
//   3. `seekg(58)` on the SSTable file and scan ONLY within [58, 115), stopping
//      early as soon as we see a key >= "erin".
// ============================================================================

class SparseIndex {
public:
  struct BlockRange {
    std::streamoff start_offset;
    std::optional<std::streamoff> end_offset; // nullopt = last block until EOF
  };

  explicit SparseIndex(std::size_t stride = 16)
      : stride_(std::max<std::size_t>(1, stride)) {}

  // Called in sorted key order while writing or loading an SSTable.
  // Records every `stride_`-th key's starting byte offset in the file.
  void add_record(std::string_view key, std::streamoff byte_offset) {
    if (record_count_ % stride_ == 0) {
      entries_.push_back(IndexEntry{std::string(key), byte_offset});
    }
    max_key_ = key;
    ++record_count_;
  }

  // Returns the [start_offset, end_offset) byte range of the single block on
  // disk that could contain `key`, or std::nullopt if `key` is outside the
  // SSTable's [min_key, max_key] bounds.
  std::optional<BlockRange> find_block(std::string_view key) const {
    if (entries_.empty() || key < entries_.front().key || key > max_key_) {
      return std::nullopt;
    }

    auto upper = std::upper_bound(
        entries_.begin(), entries_.end(), key,
        [](std::string_view target, const IndexEntry &entry) {
          return target < entry.key;
        });

    auto block_it = std::prev(upper);
    std::optional<std::streamoff> end_offset = std::nullopt;
    if (upper != entries_.end()) {
      end_offset = upper->offset;
    }
    return BlockRange{block_it->offset, end_offset};
  }

  std::size_t size() const { return entries_.size(); }
  std::size_t total_records() const { return record_count_; }
  std::size_t stride() const { return stride_; }

private:
  struct IndexEntry {
    std::string key;
    std::streamoff offset;
  };

  std::size_t stride_;
  std::size_t record_count_ = 0;
  std::vector<IndexEntry> entries_;
  std::string max_key_;
};

