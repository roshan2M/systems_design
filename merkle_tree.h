#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// Merkle Tree (Anti-Entropy Replica Sync)
// ============================================================================
//
// In distributed databases (Dynamo, Cassandra, Riak), two replicas holding
// millions of keys need to detect and repair small divergences without sending
// the entire dataset over the network.
//
// A Merkle Tree is a binary tree of cryptographic/fast hashes built over
// sorted key-value pairs:
//
//                 [ Root Hash ]                 ◄── 1. Compare roots: equal?
//                 /           \                        If yes, 100% in sync!
//          [H(0..1)]         [H(2..3)]          ◄── 2. Only descend into
//           /     \           /     \                  subtrees whose hashes
//        [k0]    [k1]      [k2]    [k3]                differ!
//
// Complexity:
//   - Identical replicas: 1 hash comparison (O(1))
//   - d differing keys among N keys: O(d * log N) node comparisons instead of
//     scanning all N keys.
// ============================================================================

class MerkleTree {
public:
  explicit MerkleTree(const std::map<std::string, std::string> &data) {
    if (data.empty()) {
      return;
    }
    std::vector<std::unique_ptr<Node>> level;
    level.reserve(data.size());
    for (const auto &[k, v] : data) {
      level.push_back(std::make_unique<Node>(Node{
          .hash = hash_leaf(k, v),
          .key = k,
          .left = nullptr,
          .right = nullptr,
      }));
    }

    // Build bottom-up until a single root remains. If a level has an odd number
    // of nodes, promote the trailing node directly to the next level.
    while (level.size() > 1) {
      std::vector<std::unique_ptr<Node>> next_level;
      next_level.reserve((level.size() + 1) / 2);
      for (std::size_t i = 0; i < level.size(); i += 2) {
        if (i + 1 < level.size()) {
          auto left = std::move(level[i]);
          auto right = std::move(level[i + 1]);
          const std::size_t combined = hash_internal(left->hash, right->hash);
          next_level.push_back(std::make_unique<Node>(Node{
              .hash = combined,
              .key = "",
              .left = std::move(left),
              .right = std::move(right),
          }));
        } else {
          next_level.push_back(std::move(level[i]));
        }
      }
      level = std::move(next_level);
    }
    root_ = std::move(level.front());
  }

  std::size_t root_hash() const { return root_ ? root_->hash : 0; }

  // Returns the sorted list of keys that differ between `this` and `other`
  // (assuming both trees were built over the same key set), and records how
  // many tree nodes were compared.
  std::vector<std::string> diff(const MerkleTree &other,
                                std::size_t *nodes_compared = nullptr) const {
    std::vector<std::string> differing_keys;
    std::size_t compared = 0;
    diff_nodes(root_.get(), other.root_.get(), differing_keys, compared);
    if (nodes_compared != nullptr) {
      *nodes_compared = compared;
    }
    return differing_keys;
  }

private:
  struct Node {
    std::size_t hash;
    std::string key; // Non-empty only for leaf nodes
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;

    bool is_leaf() const { return !left && !right; }
  };

  std::unique_ptr<Node> root_;

  static std::size_t hash_leaf(const std::string &key,
                               const std::string &value) {
    return std::hash<std::string>{}("L:" + key + '\0' + value);
  }

  static std::size_t hash_internal(std::size_t left_hash,
                                   std::size_t right_hash) {
    return std::hash<std::string>{}("I:" + std::to_string(left_hash) + ":" +
                                    std::to_string(right_hash));
  }

  static void diff_nodes(const Node *a, const Node *b,
                         std::vector<std::string> &out, std::size_t &compared) {
    if (a == nullptr && b == nullptr) {
      return;
    }
    ++compared;

    if (a == nullptr || b == nullptr) {
      collect_leaves(a ? a : b, out);
      return;
    }
    if (a->hash == b->hash) {
      return; // Entire subtree matches!
    }
    if (a->is_leaf() && b->is_leaf()) {
      if (a->key == b->key) {
        out.push_back(a->key);
      } else {
        out.push_back(a->key);
        out.push_back(b->key);
      }
      return;
    }
    diff_nodes(a->left.get(), b->left.get(), out, compared);
    diff_nodes(a->right.get(), b->right.get(), out, compared);
  }

  static void collect_leaves(const Node *node, std::vector<std::string> &out) {
    if (node == nullptr) {
      return;
    }
    if (node->is_leaf()) {
      out.push_back(node->key);
      return;
    }
    collect_leaves(node->left.get(), out);
    collect_leaves(node->right.get(), out);
  }
};
