#include "bloom_filter.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <string>

// ============================================================================
// Walkthrough Demo
// ============================================================================
int main() {
  constexpr std::size_t N = 10'000;
  constexpr double TARGET_FPR = 0.01; // 1% target false positive rate

  auto filter = BloomFilter::with_rate(N, TARGET_FPR);

  std::cout << "Configured BloomFilter for " << N << " keys @ "
            << (TARGET_FPR * 100) << "% target FPR:\n"
            << "  Bit array size (m): " << filter.num_bits() << " bits (~"
            << std::fixed << std::setprecision(1)
            << (static_cast<double>(filter.num_bits()) / N) << " bits/key, "
            << (filter.num_bits() / 8) << " bytes total)\n"
            << "  Hash functions (k): " << filter.num_hashes() << "\n\n";

  // 1. Insert N keys ("key_0" .. "key_9999").
  for (std::size_t i = 0; i < N; ++i) {
    filter.add("key_" + std::to_string(i));
  }

  // 2. Verify ZERO false negatives: every inserted key MUST return true.
  for (std::size_t i = 0; i < N; ++i) {
    assert(filter.might_contain("key_" + std::to_string(i)));
  }
  std::cout << "False negatives on " << N << " inserted keys: 0 (guaranteed)\n";

  // 3. Measure false positive rate on 100,000 absent keys.
  constexpr std::size_t QUERIES = 100'000;
  std::size_t false_positives = 0;
  for (std::size_t i = 0; i < QUERIES; ++i) {
    if (filter.might_contain("missing_" + std::to_string(i))) {
      ++false_positives;
    }
  }

  const double actual_fpr =
      100.0 * static_cast<double>(false_positives) / QUERIES;
  std::cout << "False positives on " << QUERIES
            << " absent keys: " << false_positives << " ("
            << std::setprecision(2) << actual_fpr << "%)\n"
            << "  -> In an LSM tree, " << (QUERIES - false_positives) << " / "
            << QUERIES << " pointless SSTable disk reads were skipped!\n";

  assert(actual_fpr < 2.0); // Sanity check near 1%
  return 0;
}
