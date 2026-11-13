# Skip List

> **Idea:** Stack probabilistic "express lane" linked lists on top of a sorted linked list to get `O(log N)` search and insertion without tree rotations.

A **skip list** is an ordered in-memory map/set invented by William Pugh as a simpler alternative to balanced search trees (Red-Black or AVL trees). Storage engines like LevelDB, RocksDB, and Pebble use skip lists for their **Memtables**, and Redis uses them behind sorted sets (`ZSET`).

## Structure

| Element | Role |
| --- | --- |
| **Level 0 (base list)** | Standard singly (or doubly) linked list containing *every* key in sorted order. |
| **Higher levels (`1 .. L`)** | Sparse express lanes skipping over multiple nodes at once. |
| **Tower height (`h`)** | Chosen randomly on insert by flipping a coin (`p = 1/2` or `1/4`) up to `kMaxLevel`. |

```text
Level 3:  HEAD ───────────────────────────────► [dave] ────────────► NIL
Level 2:  HEAD ──────────────► [bob] ─────────► [dave] ────────────► NIL
Level 1:  HEAD ──► [alice] ──► [bob] ─────────► [dave] ──► [erin] ─► NIL
Level 0:  HEAD ──► [alice] ──► [bob] ──► [cam] ► [dave] ──► [erin] ─► NIL
```

Because each node at level `i` is promoted to `i + 1` with probability `p = 1/2`, roughly `N/2` nodes appear at Level 1, `N/4` at Level 2, and ~1 node near `log2(N)` — matching the branching structure of a balanced binary tree on average.

## Operations

- **Search (`find`):** Start at `HEAD` on the highest active level. Walk right while `next->key < target`. When the next node would overshoot `target` (or is `NIL`), drop down one level and continue until Level 0.
- **Insert:** Run the same top-down search while recording the rightmost predecessor (`update[lvl]`) visited at every level. If the key already exists at Level 0, update its value; otherwise roll a random height `h` and splice the new node in after `update[0 .. h-1]`.
- **Delete (`erase`):** Find `update[0 .. height-1]` predecessors; if the target exists at Level 0, rewire `update[lvl]->next[lvl] = target->next[lvl]` across its tower levels.
- **Range scan / flush:** Follow `next[0]` pointers linearly from the starting node in `O(1)` per element.

## Why storage engines prefer skip lists over Red-Black trees

1. **Lock-free concurrency:** Inserting into a Red-Black tree can trigger rotations up the tree, requiring coarse locks or complex lock coupling. Inserting into a skip list only mutates forward pointers of immediate local predecessors using atomic compare-and-swap (`CAS`), allowing concurrent lock-free readers and writers.
2. **Simple sorted iteration:** Level 0 is already a flat linked list, making Memtable flushes and range iterators trivial without parent pointers or stacks.

## Quick comparison

| | Skip list | Balanced BST (`std::map`) | B-tree (in-memory) | Hash table (`std::unordered_map`) |
| --- | --- | --- | --- | --- |
| **Point lookup / insert** | `O(log N)` average | `O(log N)` worst-case | `O(log N)` worst-case | `O(1)` average |
| **Ordered / range scan** | Fast linear walk at L0 | Supported (pointer chasing) | Very cache-friendly | Not supported |
| **Lock-free writes** | Practical (`CAS` splicing) | Hard (rotations rebalance) | Complex (node splits) | Resizing complicates concurrency |

## References

- [Pugh (1990) — Skip Lists: A Probabilistic Alternative to Balanced Trees](https://15721.courses.cs.cmu.edu/spring2018/papers/08-oltpindexes1/pugh-skiplists-cacm1990.pdf)
- [LevelDB `SkipList` implementation](https://github.com/google/leveldb/blob/main/db/skiplist.h)
- [RocksDB MemTable wiki](https://github.com/facebook/rocksdb/wiki/MemTable)

