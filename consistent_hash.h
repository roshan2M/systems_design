#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ============================================================================
// Educational Consistent Hashing Ring (with Virtual Nodes)
// ============================================================================
//
// Naive sharding (`hash(key) % N`) remaps almost every key (~(N-1)/N) when a
// server is added or removed. Consistent hashing maps both servers and keys onto
// the same circular hash space [0, 2^64):
//
//                     0 (wraps around)
//                    ...
//           Node A #1   *  ──► key "user_42" walks clockwise to Node B #2
//                      / \
//        Node C #2   *     *  Node B #2
//                     \   /
//                      *
//                  Node A #2
//
//   - get_node(key): hash(key), then walk clockwise (`lower_bound`, wrapping
//     to `begin()` past the end) to the first virtual node on the ring.
//   - Virtual nodes (vnodes): each physical server owns `vnodes_per_node`
//     positions on the ring. Without vnodes, a few servers produce wildly
//     skewed partitions; with ~100-200 vnodes, load variance drops sharply and
//     removing a server spreads its keys evenly across all remaining servers.
//   - get_preference_list(key, R): walks clockwise collecting R DISTINCT
//     physical nodes for Dynamo/Cassandra-style replication.
// ============================================================================

class ConsistentHashRing {
public:
  explicit ConsistentHashRing(std::size_t vnodes_per_node = 150)
      : vnodes_per_node_(std::max<std::size_t>(1, vnodes_per_node)) {}

  void add_node(const std::string &node) {
    if (node_hashes_.contains(node)) {
      return;
    }
    std::vector<std::size_t> positions;
    positions.reserve(vnodes_per_node_);

    for (std::size_t i = 0; i < vnodes_per_node_; ++i) {
      std::size_t h = hash_vnode(node, i);
      // Probe linearly in the rare event of an exact hash collision on the ring.
      while (ring_.contains(h)) {
        ++h;
      }
      ring_.emplace(h, node);
      positions.push_back(h);
    }
    node_hashes_.emplace(node, std::move(positions));
  }

  void remove_node(const std::string &node) {
    auto it = node_hashes_.find(node);
    if (it == node_hashes_.end()) {
      return;
    }
    for (std::size_t h : it->second) {
      ring_.erase(h);
    }
    node_hashes_.erase(it);
  }

  // Returns the primary physical node responsible for `key`.
  std::string get_node(std::string_view key) const {
    if (ring_.empty()) {
      return "";
    }
    auto it = ring_.lower_bound(hash_key(key));
    if (it == ring_.end()) {
      it = ring_.begin(); // Wrap around the ring
    }
    return it->second;
  }

  // Returns up to `replication_factor` distinct physical nodes walking
  // clockwise from `hash(key)` (Dynamo / Cassandra preference list).
  std::vector<std::string> get_preference_list(std::string_view key,
                                               std::size_t replication_factor) const {
    std::vector<std::string> replicas;
    if (ring_.empty() || replication_factor == 0) {
      return replicas;
    }
    const std::size_t target = std::min(replication_factor, node_hashes_.size());
    std::set<std::string_view> seen;

    auto it = ring_.lower_bound(hash_key(key));
    for (std::size_t steps = 0; steps < ring_.size() && replicas.size() < target;
         ++steps) {
      if (it == ring_.end()) {
        it = ring_.begin();
      }
      if (seen.insert(it->second).second) {
        replicas.push_back(it->second);
      }
      ++it;
    }
    return replicas;
  }

  std::size_t node_count() const { return node_hashes_.size(); }
  std::size_t vnode_count() const { return ring_.size(); }

private:
  std::size_t vnodes_per_node_;
  std::map<std::size_t, std::string> ring_;
  std::unordered_map<std::string, std::vector<std::size_t>> node_hashes_;

  static std::size_t hash_key(std::string_view key) {
    return std::hash<std::string_view>{}(key);
  }

  static std::size_t hash_vnode(const std::string &node, std::size_t vnode_idx) {
    return std::hash<std::string>{}(node + "#" + std::to_string(vnode_idx));
  }
};

