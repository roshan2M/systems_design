#include "bloom_filter.h"
#include "skip_list.h"
#include "sparse_index.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// Log-Structured Merge (LSM) Tree
// ============================================================================
//
// An LSM tree optimizes for high write throughput by turning random writes
// into sequential disk I/O:
//
//   WRITE PATH:
//     put(k, v) / erase(k)
//        │
//        ├──► 1. Append to WAL on disk (sequential I/O for crash recovery)
//        └──► 2. Insert into Memtable in RAM (sorted SkipList)
//                    │
//                    ▼ (when Memtable reaches capacity)
//               Flush to immutable SSTable on disk, build its BloomFilter +
//               SparseIndex in RAM, & clear WAL
//
//   READ PATH:
//     get(k) checks from NEWEST to OLDEST and stops at the first match:
//        1. Memtable (SkipList in RAM, O(log M))
//        2. For each SSTable N .. 0 (newest to oldest):
//             a. BloomFilter (RAM)  ──► false? Definitely absent; skip file!
//             b. SparseIndex (RAM)  ──► binary search to 1 block's byte offset
//             c. SSTable block (Disk) ► seekg(offset) & scan <= stride records
//
//   DELETES & TOMBSTONES:
//     Because SSTables are immutable, deleting a key writes a "tombstone"
//     (std::nullopt) that shadows older values until compaction removes both.
//
//   COMPACTION:
//     Periodically merges older SSTables into a single sorted SSTable,
//     discarding overwritten values and tombstones to reclaim space and
//     keep reads fast.
// ============================================================================

class LsmTree {
public:
  // An entry's value is std::nullopt when the key has been deleted (tombstone).
  using Entry = std::optional<std::string>;
  using Table = SkipList<std::string, Entry>;

  explicit LsmTree(std::filesystem::path dir, std::size_t capacity = 1000,
                   std::size_t sparse_stride = 16)
      : dir_(std::move(dir)), capacity_(capacity),
        sparse_stride_(sparse_stride) {
    std::filesystem::create_directories(dir_);
    load_existing_state();
  }

  // Write path: append to WAL first (durability), then update Memtable (speed).
  // When the Memtable is full, flush it to a new immutable SSTable on disk.
  void put(const std::string &key, const std::string &value) {
    write(key, value);
  }

  // Deletes in an LSM tree are also writes: we record a "tombstone" (nullopt)
  // that shadows older values in earlier SSTables.
  void erase(const std::string &key) { write(key, std::nullopt); }

  // Read path: search from newest data to oldest:
  //   1. Check the in-memory SkipList Memtable first.
  //   2. Check SSTables from newest to oldest:
  //        - BloomFilter skips SSTables that definitely lack `key`.
  //        - SparseIndex jumps directly to the single disk block for `key`.
  Entry get(const std::string &key) const {
    if (auto it = memtable_.find(key); it != memtable_.end()) {
      return it->second;
    }
    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
      if (!it->filter.might_contain(key)) {
        ++bloom_skips_; // Definitely not in this SSTable — skip disk I/O!
        continue;
      }
      auto block = it->index.find_block(key);
      if (!block.has_value()) {
        ++range_skips_; // Outside [min_key, max_key] despite Bloom FP!
        continue;
      }
      if (auto found = search_sstable_block(it->path, *block, key)) {
        return *found; // Present value OR tombstone (shadows older SSTables)
      }
    }
    return std::nullopt;
  }

  std::size_t sstable_reads() const { return sstable_reads_; }
  std::size_t records_scanned() const { return records_scanned_; }
  std::size_t bloom_skips() const { return bloom_skips_; }
  std::size_t range_skips() const { return range_skips_; }

  // Flush the sorted SkipList Memtable to a new immutable SSTable file, build
  // its in-memory BloomFilter and SparseIndex, and reset the WAL.
  void flush() {
    if (memtable_.empty())
      return;

    auto path = dir_ / ("sst_" + std::to_string(next_id_++) + ".txt");
    auto filter = BloomFilter::with_rate(memtable_.size());
    SparseIndex index(sparse_stride_);

    {
      std::ofstream out(path, std::ios::binary);
      for (const auto &[key, value] : memtable_) {
        index.add_record(key, out.tellp());
        write_record(out, key, value);
        // Tombstones ARE added to the Bloom filter and SparseIndex so get()
        // finds them and stops searching older SSTables!
        filter.add(key);
      }
    }

    sstables_.push_back(
        SSTable{std::move(path), std::move(filter), std::move(index)});
    memtable_.clear();
    std::ofstream(wal_path(), std::ios::trunc); // Clear WAL
  }

  // Compaction: merge all SSTables from oldest to newest so newer entries
  // overwrite older ones, drop tombstones, and write a single compacted
  // SSTable.
  void compact() {
    flush();
    if (sstables_.size() < 2)
      return;

    Table merged;
    for (const auto &sst : sstables_) {
      for (const auto &[key, value] : read_records(sst.path)) {
        if (value.has_value()) {
          merged[key] = value; // Newer value overwrites older one
        } else {
          merged.erase(key); // Full compaction can finally discard tombstones
        }
      }
    }

    for (const auto &sst : sstables_)
      std::filesystem::remove(sst.path);
    sstables_.clear();
    next_id_ = 0;

    memtable_ = std::move(merged);
    flush();
  }

private:
  struct SSTable {
    std::filesystem::path path;
    BloomFilter filter;
    SparseIndex index;
  };

  std::filesystem::path dir_;
  std::size_t capacity_;
  std::size_t sparse_stride_;
  Table memtable_;
  std::vector<SSTable> sstables_; // Oldest to newest
  std::size_t next_id_ = 0;
  mutable std::size_t sstable_reads_ = 0;
  mutable std::size_t records_scanned_ = 0;
  mutable std::size_t bloom_skips_ = 0;
  mutable std::size_t range_skips_ = 0;

  std::filesystem::path wal_path() const { return dir_ / "wal.txt"; }

  void write(const std::string &key, const Entry &value) {
    {
      std::ofstream wal(wal_path(), std::ios::app);
      write_record(wal, key, value);
    } // Close & flush WAL before flush() might truncate it
    memtable_[key] = value;
    if (memtable_.size() >= capacity_)
      flush();
  }

  // Unified record format used by both the WAL and SSTables:
  //   1 "key" "value"   (present)
  //   0 "key" ""        (tombstone / deleted)
  static void write_record(std::ostream &out, const std::string &key,
                           const Entry &value) {
    out << (value ? '1' : '0') << ' ' << std::quoted(key) << ' '
        << std::quoted(value.value_or("")) << '\n';
  }

  static Table read_records(const std::filesystem::path &path) {
    Table records;
    std::ifstream in(path, std::ios::binary);
    char present;
    std::string key, value;
    while (in >> present >> std::quoted(key) >> std::quoted(value)) {
      records[key] = (present == '1') ? Entry(value) : std::nullopt;
    }
    return records;
  }

  // Seek directly to `block.start_offset` and scan at most one block on disk.
  // Returns:
  //   - Engaged outer optional   -> `target_key` was found in this SSTable
  //                                 (inner Entry holds string value or nullopt
  //                                 tombstone).
  //   - Disengaged outer optional -> `target_key` is not in this SSTable.
  std::optional<Entry>
  search_sstable_block(const std::filesystem::path &path,
                       const SparseIndex::BlockRange &block,
                       const std::string &target_key) const {
    ++sstable_reads_;
    std::ifstream in(path, std::ios::binary);
    in.seekg(block.start_offset);

    while ((in >> std::ws) && in.peek() != std::char_traits<char>::eof()) {
      if (block.end_offset && in.tellg() >= *block.end_offset) {
        break; // Reached start of the next block
      }
      char present;
      std::string key, value;
      if (!(in >> present >> std::quoted(key) >> std::quoted(value))) {
        break;
      }
      ++records_scanned_;
      if (key == target_key) {
        return (present == '1') ? Entry(value) : Entry(std::nullopt);
      }
      if (key > target_key) {
        break; // SSTable is sorted: target_key cannot appear past here
      }
    }
    return std::nullopt;
  }

  void load_existing_state() {
    // Load any existing SSTable files in order (rebuilding each SSTable's
    // in-memory BloomFilter + SparseIndex) and replay the WAL into Memtable.
    while (std::filesystem::exists(
        dir_ / ("sst_" + std::to_string(next_id_) + ".txt"))) {
      auto path = dir_ / ("sst_" + std::to_string(next_id_++) + ".txt");

      std::vector<std::pair<std::string, std::streamoff>> key_offsets;
      {
        std::ifstream in(path, std::ios::binary);
        while ((in >> std::ws) && in.peek() != std::char_traits<char>::eof()) {
          const std::streamoff offset = in.tellg();
          char present;
          std::string key, value;
          if (!(in >> present >> std::quoted(key) >> std::quoted(value))) {
            break;
          }
          key_offsets.emplace_back(std::move(key), offset);
        }
      }

      auto filter = BloomFilter::with_rate(key_offsets.size());
      SparseIndex index(sparse_stride_);
      for (const auto &[key, offset] : key_offsets) {
        filter.add(key);
        index.add_record(key, offset);
      }
      sstables_.push_back(
          SSTable{std::move(path), std::move(filter), std::move(index)});
    }
    memtable_ = read_records(wal_path());
  }
};

// ============================================================================
// Walkthrough Demo
// ============================================================================
int main() {
  const std::filesystem::path data_dir = "/tmp/lsm_demo";
  std::filesystem::remove_all(data_dir);

  // Part A: Demonstrate BloomFilter + SparseIndex read-path efficiency across
  // multiple SSTables (4 SSTables x 64 keys = 256 keys, sparse stride = 8).
  {
    LsmTree perf_db(data_dir, /*capacity=*/64, /*sparse_stride=*/8);
    for (int i = 0; i < 256; ++i) {
      std::ostringstream key;
      key << "user_" << std::setw(4) << std::setfill('0') << i;
      perf_db.put(key.str(), "score_" + std::to_string(i));
    }

    // Look up a key in the oldest SSTable ("user_0045"): BloomFilter skips the
    // 3 newer SSTables, and SparseIndex jumps straight to block [40..47] in
    // sst_0.txt, scanning only 6 records instead of 64!
    assert(perf_db.get("user_0045") == "score_45");
    std::cout << "Lookup 'user_0045' across 4 SSTables (256 total keys, "
                 "stride = 8):\n"
              << "  SSTables skipped by BloomFilter : " << perf_db.bloom_skips()
              << " / 3 newer SSTables\n"
              << "  SSTable files opened            : "
              << perf_db.sstable_reads() << "\n"
              << "  Records scanned on disk         : "
              << perf_db.records_scanned()
              << " (<= 8 in 1 block, vs 256 without indexes!)\n\n";
    std::filesystem::remove_all(data_dir);
  }

  // Part B: Step-by-step functional walkthrough (writes, updates, tombstones,
  // WAL crash-recovery replay, and compaction).
  {
    LsmTree db(data_dir, /*capacity=*/2, /*sparse_stride=*/2);

    // 1. Two writes fill the SkipList Memtable (capacity = 2) -> sst_0.txt.
    db.put("alice", "100");
    db.put("bob", "200");

    // 2. Update "alice" and delete "bob" -> sst_1.txt (with a tombstone for
    //    "bob" that shadows sst_0.txt).
    db.put("alice", "150");
    db.erase("bob");

    // 3. One more write stays in the SkipList Memtable + WAL (not yet flushed).
    db.put("charlie", "300");

    assert(db.get("alice") == "150");
    assert(db.get("bob") == std::nullopt);
    assert(db.get("charlie") == "300");
  }

  {
    // 4. Reopen database: rebuilds BloomFilters + SparseIndexes for sst_0.txt
    //    and sst_1.txt, and replays wal.txt into the SkipList Memtable.
    LsmTree db(data_dir, /*capacity=*/2, /*sparse_stride=*/2);
    assert(db.get("alice") == "150");
    assert(db.get("bob") == std::nullopt);
    assert(db.get("charlie") == "300");

    // 5. Compact all SSTables into a single sst_0.txt, discarding old values
    //    and tombstones.
    db.compact();
    assert(db.get("alice") == "150");
    assert(db.get("bob") == std::nullopt);
    assert(db.get("charlie") == "300");
  }

  // 6. Print the contents of all files in data_dir, then clean up.
  std::cout << "LSM Tree demo passed! Files in " << data_dir << ":\n";
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(data_dir)) {
    files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());

  for (const auto &file : files) {
    std::cout << "\n--- " << file.filename().string() << " ---\n";
    std::ifstream in(file);
    if (in.peek() == std::ifstream::traits_type::eof()) {
      std::cout << "(empty)\n";
    } else {
      std::cout << in.rdbuf();
    }
  }

  std::filesystem::remove_all(data_dir);
  std::cout << "\nCleaned up " << data_dir << "\n";
  return 0;
}
