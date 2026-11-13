#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ============================================================================
// Educational Bloom Filter
// ============================================================================
//
// A Bloom filter is a space-efficient, probabilistic set membership structure:
//
//   might_contain(key) == false  ──► DEFINITELY NOT in the set (0% false neg)
//   might_contain(key) == true   ──► PROBABLY in the set       (~p% false pos)
//
// How it works:
//   - Bit array of `m` bits, initialized to all 0s, and `k` hash functions.
//   - add(key):           compute k bit positions and set all k bits to 1.
//   - might_contain(key): compute the same k bit positions; if ANY bit is 0,
//                         `key` was never added.
// ============================================================================

class BloomFilter {
public:
  // Construct explicitly with `num_bits` (m) and `num_hashes` (k).
  explicit BloomFilter(std::size_t num_bits = 1024, std::size_t num_hashes = 7)
      : bits_(std::max<std::size_t>(1, num_bits), false),
        num_hashes_(std::max<std::size_t>(1, num_hashes)) {}

  // Factory: size optimal `m` and `k` for `expected_items` (n) and target
  // `false_positive_rate` (p), using standard Bloom filter formulas:
  //   m = -n * ln(p) / (ln 2)^2
  //   k = (m / n) * ln 2
  static BloomFilter with_rate(std::size_t expected_items,
                               double false_positive_rate = 0.01) {
    expected_items = std::max<std::size_t>(1, expected_items);
    const double ln2 = std::log(2.0);
    const auto m = static_cast<std::size_t>(std::ceil(
        -(static_cast<double>(expected_items) * std::log(false_positive_rate)) /
        (ln2 * ln2)));
    const auto k = static_cast<std::size_t>(std::max(
        1.0, std::round((static_cast<double>(m) / expected_items) * ln2)));
    return BloomFilter(m, k);
  }

  void add(std::string_view key) {
    const auto [h1, h2] = hash_pair(key);
    for (std::size_t i = 0; i < num_hashes_; ++i) {
      bits_[nth_bit(h1, h2, i)] = true;
    }
  }

  bool might_contain(std::string_view key) const {
    const auto [h1, h2] = hash_pair(key);
    for (std::size_t i = 0; i < num_hashes_; ++i) {
      if (!bits_[nth_bit(h1, h2, i)]) {
        return false; // Definitely not present
      }
    }
    return true; // Probably present
  }

  std::size_t num_bits() const { return bits_.size(); }
  std::size_t num_hashes() const { return num_hashes_; }

private:
  std::vector<bool> bits_; // Compact bitset (1 bit per element)
  std::size_t num_hashes_;

  // Kirsch-Mitzenmacher double hashing (used by RocksDB / LevelDB):
  // Instead of computing `k` independent hash functions, two hashes (h1, h2)
  // simulate k hashes via: g_i(x) = (h1 + i * h2) mod m
  // with the same asymptotic false positive rate!
  std::size_t nth_bit(std::size_t h1, std::size_t h2, std::size_t i) const {
    return (h1 + i * h2) % bits_.size();
  }

  static std::pair<std::size_t, std::size_t> hash_pair(std::string_view key) {
    // Use standard library std::hash (<functional>) with two distinct prefixes
    // ("1:" vs "2:") to act as two independent seeded hashes—no external deps
    // or bit-mixing magic numbers required.
    // (Note: std::hash isn't guaranteed portable across platforms/runs, so
    // persisted SSTables in production use MurmurHash3 or xxHash instead.)
    std::hash<std::string> hasher;
    std::size_t h1 = hasher("1:" + std::string(key));
    std::size_t h2 = hasher("2:" + std::string(key));
    if (h2 == 0)
      h2 = 1; // Avoid step size of 0
    return {h1, h2};
  }
};
