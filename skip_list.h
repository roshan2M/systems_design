#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <random>
#include <type_traits>
#include <utility>

// ============================================================================
// Educational Skip List (Ordered Key-Value Map)
// ============================================================================
//
// A Skip List is a probabilistic alternative to balanced trees (Red-Black /
// AVL) built from layered sorted linked lists:
//
//   Level 3:  HEAD ───────────────────────────────► [dave] ────────────► NIL
//   Level 2:  HEAD ──────────────► [bob] ─────────► [dave] ────────────► NIL
//   Level 1:  HEAD ──► [alice] ──► [bob] ─────────► [dave] ──► [erin] ─► NIL
//   Level 0:  HEAD ──► [alice] ──► [bob] ──► [cam] ► [dave] ──► [erin] ─► NIL
//
//   - Level 0 contains EVERY key in sorted order.
//   - Each higher level acts as an "express lane": a node promoted to level L
//     is promoted to L+1 with probability p = 1/2 (like flipping a coin).
//   - Search starts at the highest active level, scanning right until the next
//     key would overshoot the target, then drops down one level and repeats.
//
// Complexity:
//   - Search / Insert / Erase: O(log N) average, O(1) sequential scan at L0.
//
// Why LSM Trees (LevelDB, RocksDB, Pebble) prefer Skip Lists for Memtables:
//   1. No tree rotations or global rebalancing on writes — inserting only
//      updates forward pointers of immediate predecessors at each level, making
//      lock-free concurrent writes (via atomic compare-and-swap) practical.
//   2. Level 0 is already a flat sorted linked list, so flushing the Memtable
//      to an SSTable on disk is a simple linear walk.
// ============================================================================

template <typename K, typename V> class SkipList {
public:
  static constexpr std::size_t kMaxLevel = 16; // Good for ~65k+ keys at p = 0.5
  using value_type = std::pair<const K, V>;

private:
  struct Node {
    value_type kv;
    std::size_t height;
    std::array<Node *, kMaxLevel> next{};

    template <typename KeyArg, typename ValArg>
    Node(KeyArg &&k, ValArg &&v, std::size_t h)
        : kv(std::forward<KeyArg>(k), std::forward<ValArg>(v)), height(h) {}
  };

  template <bool IsConst> class Iter {
  public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = SkipList::value_type;
    using pointer = std::conditional_t<IsConst, const value_type *, value_type *>;
    using reference =
        std::conditional_t<IsConst, const value_type &, value_type &>;
    using NodePtr = std::conditional_t<IsConst, const Node *, Node *>;

    explicit Iter(NodePtr node = nullptr) : node_(node) {}

    // Allow implicit conversion from non-const iterator to const_iterator.
    template <bool WasConst, typename = std::enable_if_t<IsConst && !WasConst>>
    Iter(const Iter<WasConst> &other) : node_(other.node_) {}

    reference operator*() const { return node_->kv; }
    pointer operator->() const { return &node_->kv; }

    Iter &operator++() {
      node_ = node_->next[0];
      return *this;
    }
    Iter operator++(int) {
      Iter tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator==(const Iter &a, const Iter &b) {
      return a.node_ == b.node_;
    }
    friend bool operator!=(const Iter &a, const Iter &b) {
      return a.node_ != b.node_;
    }

  private:
    friend class SkipList;
    NodePtr node_;
  };

public:
  using iterator = Iter<false>;
  using const_iterator = Iter<true>;

  SkipList() : head_(new Node(K{}, V{}, kMaxLevel)), rng_(std::random_device{}()) {}

  ~SkipList() {
    clear();
    delete head_;
  }

  SkipList(const SkipList &other) : SkipList() {
    for (const auto &[k, v] : other) {
      insert_or_assign(k, v);
    }
  }

  SkipList &operator=(const SkipList &other) {
    if (this != &other) {
      SkipList copy(other);
      swap(copy);
    }
    return *this;
  }

  SkipList(SkipList &&other) noexcept : SkipList() { swap(other); }

  SkipList &operator=(SkipList &&other) noexcept {
    if (this != &other) {
      clear();
      swap(other);
    }
    return *this;
  }

  void swap(SkipList &other) noexcept {
    std::swap(head_, other.head_);
    std::swap(height_, other.height_);
    std::swap(size_, other.size_);
    std::swap(rng_, other.rng_);
    std::swap(last_search_hops_, other.last_search_hops_);
  }

  // Insert `key -> value`, or overwrite the existing value if `key` exists.
  void insert_or_assign(const K &key, V value) {
    std::array<Node *, kMaxLevel> update{};
    Node *curr = find_predecessors(key, update);

    if (curr != nullptr && curr->kv.first == key) {
      curr->kv.second = std::move(value);
      return;
    }

    const std::size_t node_height = random_height();
    if (node_height > height_) {
      for (std::size_t lvl = height_; lvl < node_height; ++lvl) {
        update[lvl] = head_;
      }
      height_ = node_height;
    }

    Node *fresh = new Node(key, std::move(value), node_height);
    for (std::size_t lvl = 0; lvl < node_height; ++lvl) {
      fresh->next[lvl] = update[lvl]->next[lvl];
      update[lvl]->next[lvl] = fresh;
    }
    ++size_;
  }

  // Subscript access matching std::map::operator[].
  V &operator[](const K &key) {
    std::array<Node *, kMaxLevel> update{};
    Node *curr = find_predecessors(key, update);
    if (curr != nullptr && curr->kv.first == key) {
      return curr->kv.second;
    }

    const std::size_t node_height = random_height();
    if (node_height > height_) {
      for (std::size_t lvl = height_; lvl < node_height; ++lvl) {
        update[lvl] = head_;
      }
      height_ = node_height;
    }

    Node *fresh = new Node(key, V{}, node_height);
    for (std::size_t lvl = 0; lvl < node_height; ++lvl) {
      fresh->next[lvl] = update[lvl]->next[lvl];
      update[lvl]->next[lvl] = fresh;
    }
    ++size_;
    return fresh->kv.second;
  }

  iterator find(const K &key) {
    Node *node = find_node(key);
    return iterator(node);
  }

  const_iterator find(const K &key) const {
    const Node *node = find_node(key);
    return const_iterator(node);
  }

  bool erase(const K &key) {
    std::array<Node *, kMaxLevel> update{};
    Node *target = find_predecessors(key, update);
    if (target == nullptr || target->kv.first != key) {
      return false;
    }

    for (std::size_t lvl = 0; lvl < target->height; ++lvl) {
      if (update[lvl]->next[lvl] == target) {
        update[lvl]->next[lvl] = target->next[lvl];
      }
    }
    delete target;
    --size_;

    while (height_ > 1 && head_->next[height_ - 1] == nullptr) {
      --height_;
    }
    return true;
  }

  void clear() {
    Node *curr = head_->next[0];
    while (curr != nullptr) {
      Node *next = curr->next[0];
      delete curr;
      curr = next;
    }
    head_->next.fill(nullptr);
    height_ = 1;
    size_ = 0;
  }

  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  std::size_t height() const { return height_; }
  std::size_t last_search_hops() const { return last_search_hops_; }

  iterator begin() { return iterator(head_->next[0]); }
  iterator end() { return iterator(nullptr); }
  const_iterator begin() const { return const_iterator(head_->next[0]); }
  const_iterator end() const { return const_iterator(nullptr); }

private:
  Node *head_;
  std::size_t height_ = 1;
  std::size_t size_ = 0;
  std::mt19937_64 rng_;
  mutable std::size_t last_search_hops_ = 0;

  // Flip a fair coin (p = 0.5) to choose tower height in [1, kMaxLevel].
  std::size_t random_height() {
    std::size_t h = 1;
    std::uint64_t bits = rng_();
    while (h < kMaxLevel && (bits & 1ULL) == 1ULL) {
      ++h;
      bits >>= 1;
    }
    return h;
  }

  // Walk from top level down to level 0, recording the rightmost node < `key`
  // at each level in `update`. Returns the candidate node >= `key` at level 0.
  Node *find_predecessors(const K &key,
                          std::array<Node *, kMaxLevel> &update) const {
    Node *curr = head_;
    std::size_t hops = 0;
    for (std::size_t lvl = height_; lvl-- > 0;) {
      while (curr->next[lvl] != nullptr && curr->next[lvl]->kv.first < key) {
        curr = curr->next[lvl];
        ++hops;
      }
      update[lvl] = curr;
    }
    last_search_hops_ = hops;
    return curr->next[0];
  }

  Node *find_node(const K &key) const {
    std::array<Node *, kMaxLevel> unused{};
    Node *candidate = find_predecessors(key, unused);
    if (candidate != nullptr && candidate->kv.first == key) {
      return candidate;
    }
    return nullptr;
  }
};

