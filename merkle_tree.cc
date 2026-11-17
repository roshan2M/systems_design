#include "merkle_tree.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static std::string make_key(int i) {
  std::ostringstream oss;
  oss << "key_" << std::setw(4) << std::setfill('0') << i;
  return oss.str();
}

// ============================================================================
// Walkthrough Demo
// ============================================================================
int main() {
  constexpr int N = 1'024;
  std::map<std::string, std::string> replica_a;
  std::map<std::string, std::string> replica_b;

  for (int i = 0; i < N; ++i) {
    const std::string k = make_key(i);
    const std::string v = "val_" + std::to_string(i);
    replica_a[k] = v;
    replica_b[k] = v;
  }

  // 1. Identical replicas: only 1 root hash comparison needed.
  MerkleTree tree_a(replica_a);
  MerkleTree tree_b_clean(replica_b);

  std::size_t compared = 0;
  auto diffs = tree_a.diff(tree_b_clean, &compared);
  assert(diffs.empty());
  assert(compared == 1);
  std::cout << "Identical replicas (" << N << " keys):\n"
            << "  Nodes compared: " << compared << ", diffs found: 0\n\n";

  // 2. Introduce 2 stale/corrupted keys on replica_b out of 1,024 keys.
  replica_b["key_0042"] = "stale_value";
  replica_b["key_0917"] = "corrupted_value";
  MerkleTree tree_b_dirty(replica_b);

  diffs = tree_a.diff(tree_b_dirty, &compared);
  const std::vector<std::string> expected = {"key_0042", "key_0917"};
  assert(diffs == expected);

  std::cout << "2 diverged keys out of " << N << ":\n"
            << "  Detected keys : " << diffs[0] << ", " << diffs[1] << "\n"
            << "  Nodes compared: " << compared << " (vs " << N
            << " full-table key comparisons!)\n";

  assert(compared < 50);  // ~2 * 2 * log2(1024) ≈ 40 nodes max
  return 0;
}
