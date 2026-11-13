# Sparse Index

> **Idea:** Because a disk file is already sorted by key, store the file offset of only every `N`-th key (one per block) in RAM and scan just that single block on disk.

A **dense index** keeps an entry in memory for *every* record on disk — fast, but too large when storing hundreds of millions of keys. A **sparse index** (block index) exploits the fact that SSTables and sorted segment files store keys in order: knowing the start offset of each fixed-size **data block** is enough to locate any key with a single disk read.

## Structure

| Part | Location | Role |
| --- | --- | --- |
| **Data blocks** | Disk | Contiguous sorted chunks of key-value records (e.g., ~4 KB–64 KB or `N` records each). |
| **Index entries** | RAM (and SSTable footer) | Sorted array of `(first_key_in_block, byte_offset)` pairs — 1 entry per block. |
| **Key bounds** | RAM / metadata | `[min_key, max_key]` of the file to reject out-of-range lookups immediately. |

```text
Sparse Index (RAM)               Sorted SSTable File (Disk)
┌─────────┬────────┐             ┌──────────────────────────┐
│ "alice" │ off  0 │ ──────────► │ 0:   1 "alice"   "100"   │ ┐
│ "dave"  │ off 58 │ ──────┐     │ 19:  1 "bob"     "200"   │ │ Block 0
│ "grace" │ off 115│ ────┐ │     │ 38:  1 "charlie" "300"   │ ┘
└─────────┴────────┘     │ └────►│ 58:  1 "dave"    "400"   │ ┐
                         │       │ 77:  1 "erin"    "500"   │ │ Block 1
                         │       │ 96:  1 "frank"   "600"   │ ┘
                         └──────►│ 115: 1 "grace"   "700"   │ ── Block 2
                                 └──────────────────────────┘
```

## Operations

- **Build (during SSTable flush):** Stream sorted records sequentially to disk. Whenever a new block begins (every `N` records or every `4 KB`), append `(key, current_byte_offset)` to the sparse index.
- **Point lookup (`get`):**
  1. **Bounds check:** If `target < min_key` or `target > max_key`, return not found with zero disk I/O.
  2. **Binary search in RAM:** Find the last index entry whose `key <= target` (`upper_bound - 1`). Its offset is `start_offset`, and the next entry's offset is `end_offset`.
  3. **Single-block read:** Seek to `start_offset` on disk and scan records only within `[start_offset, end_offset)`. Stop early as soon as `record_key == target` (hit) or `record_key > target` (miss, since keys are sorted).
- **Range scan `[low, high]`:** Use the sparse index once to jump directly to `low`'s block, then stream forward sequentially across blocks until passing `high`.

## Block size trade-off

- **Smaller blocks (e.g., 4 KB):** Faster point lookups (less data to decompress and scan per block), but more index entries in RAM and slightly lower compression ratios.
- **Larger blocks (e.g., 64 KB):** Tiny RAM footprint and better block compression (Snappy/Zstd), at the cost of reading/decompressing more bytes per point lookup.

*(Note: A sparse index **requires** the underlying file on disk to be sorted by key. Append-only unsorted logs like Bitcask must use a dense hash index instead, whereas Kafka uses a sparse offset index because message offsets are monotonically increasing!)*

## Quick comparison

| | Sparse index (SSTable / Kafka) | Dense hash index (Bitcask) | Bloom filter |
| --- | --- | --- | --- |
| **Requires sorted file?** | **Yes** (by key or monotonic offset) | No (works on append-only logs) | No |
| **RAM footprint** | Tiny (1 entry per 4–64 KB block) | Large (1 entry per live key) | Very small (~10 bits per key) |
| **What it answers** | *Where* on disk to read (1 block) | *Exact* byte offset of key | *Whether* to skip the file completely |

## References

- [LevelDB SSTable format (data blocks & index block)](https://github.com/google/leveldb/blob/main/doc/table_format.md)
- [RocksDB Block-Based Table Format](https://github.com/facebook/rocksdb/wiki/RocksDB-BlockBasedTable-Format)
- [Kleppmann — Designing Data-Intensive Applications, Ch. 3 (SSTables and LSM-Trees)](https://dataintensive.net/)

