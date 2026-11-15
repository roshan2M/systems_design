#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>

// ============================================================================
// Vector Clock (Causal Ordering & Conflict Detection)
// ============================================================================
//
// In leaderless/multi-leader systems (Dynamo, Riak), physical wall clocks
// cannot be trusted to order events due to clock skew. A Vector Clock tracks
// logical causality as a map {node_id -> counter}:
//
//   - Local event / write at node X: increment clock[X].
//   - Replicate / receive message with clock C_in:
//       merge element-wise max: clock[n] = max(clock[n], C_in[n]) for all n.
//
// Comparing two vector clocks A and B:
//   - Equal      : A[n] == B[n] for all n.
//   - Before     : A <= B for all n, and A != B  (A causally preceded B).
//   - After      : B <= A for all n, and A != B  (A causally followed B).
//   - Concurrent : A < B on some node(s) AND A > B on other(s)
//                  ──► Neither happened first: WRITE CONFLICT (siblings)!
// ============================================================================

class VectorClock {
public:
  enum class Ordering { Equal, Before, After, Concurrent };

  // Increment this node's logical counter on a local write/event.
  void tick(const std::string &node_id) { ++clocks_[node_id]; }

  // Element-wise maximum merge with another vector clock.
  void merge(const VectorClock &other) {
    for (const auto &[node, count] : other.clocks_) {
      clocks_[node] = std::max(clocks_[node], count);
    }
  }

  static VectorClock merged(const VectorClock &a, const VectorClock &b) {
    VectorClock out = a;
    out.merge(b);
    return out;
  }

  // Determines causal relationship of `this` relative to `other`.
  Ordering compare(const VectorClock &other) const {
    bool this_less = false;
    bool other_less = false;

    // Walk both sorted maps simultaneously in O(|A| + |B|).
    auto it_a = clocks_.begin();
    auto it_b = other.clocks_.begin();
    while (it_a != clocks_.end() || it_b != other.clocks_.end()) {
      std::uint64_t val_a = 0;
      std::uint64_t val_b = 0;

      if (it_b == other.clocks_.end() ||
          (it_a != clocks_.end() && it_a->first < it_b->first)) {
        val_a = it_a->second;
        ++it_a;
      } else if (it_a == clocks_.end() || it_b->first < it_a->first) {
        val_b = it_b->second;
        ++it_b;
      } else {
        val_a = it_a->second;
        val_b = it_b->second;
        ++it_a;
        ++it_b;
      }

      if (val_a < val_b) {
        this_less = true;
      } else if (val_a > val_b) {
        other_less = true;
      }
      if (this_less && other_less) {
        return Ordering::Concurrent;
      }
    }

    if (!this_less && !other_less) {
      return Ordering::Equal;
    }
    return this_less ? Ordering::Before : Ordering::After;
  }

  std::uint64_t get(const std::string &node_id) const {
    auto it = clocks_.find(node_id);
    return (it != clocks_.end()) ? it->second : 0;
  }

  std::string to_string() const {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto &[node, count] : clocks_) {
      if (!first) {
        oss << ", ";
      }
      oss << node << ":" << count;
      first = false;
    }
    oss << "}";
    return oss.str();
  }

private:
  std::map<std::string, std::uint64_t> clocks_;
};
