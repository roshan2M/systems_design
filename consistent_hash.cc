#include "consistent_hash.h"

#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static void print_distribution(const ConsistentHashRing &ring, int num_keys) {
  std::map<std::string, int> counts;
  for (int i = 0; i < num_keys; ++i) {
    counts[ring.get_node("key_" + std::to_string(i))]++;
  }
  for (const auto &[node, count] : counts) {
    const double pct = 100.0 * count / num_keys;
    std::cout << "    " << node << ": " << std::setw(5) << count << " keys ("
              << std::fixed << std::setprecision(1) << pct << "%)\n";
  }
}

// ============================================================================
// Walkthrough Demo
// ============================================================================
int main() {
  constexpr int NUM_KEYS = 10'000;
  const std::vector<std::string> initial_nodes = {"node-a", "node-b", "node-c"};

  // 1. Compare load balance: 1 vnode per node vs 150 vnodes per node.
  ConsistentHashRing no_vnodes(/*vnodes_per_node=*/1);
  ConsistentHashRing with_vnodes(/*vnodes_per_node=*/150);
  for (const auto &n : initial_nodes) {
    no_vnodes.add_node(n);
    with_vnodes.add_node(n);
  }

  std::cout << "Key distribution across 3 nodes (" << NUM_KEYS << " keys):\n";
  std::cout << "  Without virtual nodes (vnodes = 1):\n";
  print_distribution(no_vnodes, NUM_KEYS);
  std::cout << "  With virtual nodes (vnodes = 150):\n";
  print_distribution(with_vnodes, NUM_KEYS);

  // 2. Measure key movement when adding a 4th node: modulo vs consistent hash.
  std::vector<std::string> before_ch(NUM_KEYS);
  std::vector<std::size_t> before_mod(NUM_KEYS);
  for (int i = 0; i < NUM_KEYS; ++i) {
    const std::string key = "key_" + std::to_string(i);
    before_ch[i] = with_vnodes.get_node(key);
    before_mod[i] = std::hash<std::string>{}(key) % 3;
  }

  with_vnodes.add_node("node-d");

  int moved_ch = 0;
  int moved_mod = 0;
  for (int i = 0; i < NUM_KEYS; ++i) {
    const std::string key = "key_" + std::to_string(i);
    if (with_vnodes.get_node(key) != before_ch[i]) {
      ++moved_ch;
    }
    if ((std::hash<std::string>{}(key) % 4) != before_mod[i]) {
      ++moved_mod;
    }
  }

  const double ch_pct = 100.0 * moved_ch / NUM_KEYS;
  const double mod_pct = 100.0 * moved_mod / NUM_KEYS;
  std::cout << "\nAdding 'node-d' (3 -> 4 nodes):\n"
            << "  Naive hash(k) % N moved : " << moved_mod << " / " << NUM_KEYS
            << " (" << std::setprecision(1) << mod_pct << "%)\n"
            << "  ConsistentHashRing moved: " << moved_ch << " / " << NUM_KEYS
            << " (" << ch_pct << "%, ideal ~25.0%)\n";

  assert(ch_pct < 35.0);
  assert(mod_pct > 65.0);

  // 3. Verify replication preference list returns 3 distinct physical nodes.
  auto replicas = with_vnodes.get_preference_list("user_42", 3);
  assert(replicas.size() == 3);
  assert(replicas[0] != replicas[1] && replicas[1] != replicas[2] &&
         replicas[0] != replicas[2]);

  std::cout << "\nPreference list (R=3) for 'user_42': " << replicas[0] << ", "
            << replicas[1] << ", " << replicas[2] << "\n";

  // 4. Removing a node reassigns its keys without touching others.
  with_vnodes.remove_node("node-d");
  for (int i = 0; i < NUM_KEYS; ++i) {
    const std::string key = "key_" + std::to_string(i);
    assert(with_vnodes.get_node(key) == before_ch[i]);
  }
  std::cout << "Removed 'node-d': 100% of keys returned to original owners!\n";
  return 0;
}
