# LSM Tree

> **Idea:** Buffer writes in memory, flush sorted files to disk, and merge those files in the background.

**LSM** (Log-Structured Merge) trees are ordered indexes optimized for write-heavy workloads. They support point lookups and range scans. Implementations vary; this is the common model.

## Components

| Part | Role |
| --- | --- |
| **Memtable** | Ordered in-memory buffer for recent writes; often a skip list. Volatile and size-limited. |
| **WAL** | Append-only recovery log for changes not yet flushed. Durability depends on syncing the log before acknowledging writes. |
| **SSTables** | Immutable, key-sorted files on disk. |
| **Compaction** | Merges SSTables, reducing obsolete data and read overhead. |

```text
┌───────┐     ┌───────────┐     ┌──────────────┐            ┌───────────────┐     ┌───────────────────────┐
│ Write │ ──► │    WAL    │ ──► │   Memtable   │ ──(Full)─► │ SSTable flush │ ──► │ Background compaction │
└───────┘     │  (disk)   │     │    (RAM)     │            │    (disk)     │     │        (disk)         │
              └───────────┘     └──────────────┘            └───────────────┘     └───────────────────────┘
```

## Operations

- **Write/update:** Append to the WAL and insert into the active memtable. When full, freeze and flush it to SSTable(s); start a new memtable.
- **Delete:** Write a **tombstone** (deletion marker). It hides older values until compaction can safely remove them.
- **Read:** Check active and pending memtables, then relevant SSTables. Return the newest visible version; a tombstone means “not found.” Search order follows the engine’s levels and version metadata, not simply file creation time.
- **Range scan:** Merge ordered iterators from memory and disk, resolving duplicate versions and tombstones.

## SSTable lookup aids

SSTables store sorted data in blocks. A sparse/block index maps key ranges to block offsets. A **Bloom filter** can say “definitely absent” or “possibly present”; a positive result still needs a lookup. Metadata may store key bounds and statistics. These structures may be blocks within the SSTable rather than separate files.

## Compaction

Compaction performs a k-way merge of sorted inputs and writes one or more new SSTables. It discards overwritten versions when safe and removes tombstones only when older values cannot reappear. Compaction costs CPU and disk bandwidth; if it falls behind, writes may stall.

- **Leveled compaction:** Usually reduces read and space overhead, with more rewriting.
- **Tiered compaction:** Usually reduces rewriting, with more read and space overhead.

## Quick comparison

| | LSM tree | B-tree / B+ tree | Hash index |
| --- | --- | --- | --- |
| **Writes** | Often strong for sustained writes; compaction adds work | Updates and may split/rebalance pages | Efficient equality lookups; collisions and resizing matter |
| **Reads** | May check several runs; filters help | Predictable ordered lookup | Equality only |
| **Ranges** | Supported; merge work | Efficient ordered traversal | Not efficient |
| **Data must fit RAM?** | No | No | No for disk-based indexes; yes for an in-memory hash table |

These are workload-dependent tendencies, not universal speed rankings. LSM trees trade extra read/write/space amplification for write throughput. Hash indexes do not inherently provide O(1) disk I/O.

## References

- [RocksDB overview](https://github.com/facebook/rocksdb/wiki/RocksDB-Overview) · [WAL](https://github.com/facebook/rocksdb/wiki/Write-Ahead-Log-%28WAL%29)
- [LevelDB table format](https://github.com/google/leveldb/blob/main/doc/table_format.md) · [Compaction](https://github.com/facebook/rocksdb/wiki/Compaction)
- [PostgreSQL index types](https://www.postgresql.org/docs/current/indexes-types.html) · [Hash indexes](https://www.postgresql.org/docs/current/hash-index.html)
