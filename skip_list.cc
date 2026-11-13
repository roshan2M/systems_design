#include "skip_list.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// Walkthrough Demo
// ============================================================================
int main() {
  SkipList<std::string, int> sl;

  // 1. Insert out-of-order keys and verify sorted Level-0 iteration.
  sl["dave"] = 40;
  sl["alice"] = 10;
  sl["erin"] = 50;
  sl["bob"] = 20;
  sl["cam"] = 30;
  sl["bob"] = 25; // Overwrite existing key

  assert(sl.size() == 5);
  assert(sl.find("bob") != sl.end() && sl.find("bob")->second == 25);
  assert(sl.find("missing") == sl.end());

  std::vector<std::string> keys_in_order;
  for (const auto &[k, _] : sl) {
    keys_in_order.push_back(k);
  }
  const std::vector<std::string> expected = {"alice", "bob", "cam", "dave",
                                             "erin"};
  assert(keys_in_order == expected);

  // 2. Erase a key and confirm remaining keys stay linked in order.
  assert(sl.erase("cam") == true);
  assert(sl.erase("cam") == false);
  assert(sl.find("cam") == sl.end());
  assert(sl.size() == 4);

  // 3. Benchmark O(log N) express-lane hops on 10,000 keys vs O(N) linked list.
  constexpr int N = 10'000;
  SkipList<int, int> bench;
  for (int i = 0; i < N; ++i) {
    bench.insert_or_assign(i, i * 10);
  }

  std::size_t total_hops = 0;
  for (int i = 0; i < N; ++i) {
    auto it = bench.find(i);
    assert(it != bench.end() && it->second == i * 10);
    total_hops += bench.last_search_hops();
  }

  const double avg_hops = static_cast<double>(total_hops) / N;
  std::cout << "SkipList with " << N << " keys:\n"
            << "  Active tower height : " << bench.height() << " / "
            << SkipList<int, int>::kMaxLevel << " levels\n"
            << "  Avg forward hops    : " << std::fixed << std::setprecision(1)
            << avg_hops << " hops/lookup (vs ~" << (N / 2)
            << " in a plain sorted linked list!)\n";

  assert(avg_hops < 40.0); // Expected ~log2(10000) = ~13-15 forward hops
  return 0;
}

