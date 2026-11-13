# Bloom Filter

> **Idea:** Hash each key into a compact bit array so you can quickly prove a key is *definitely absent* without storing the keys themselves.

A **Bloom filter** is a space-efficient, probabilistic set-membership structure. It never produces **false negatives** (an inserted key is always reported as possibly present), at the cost of a tunable **false positive** rate (an absent key may look present if its bits collided with other keys). Storage engines like RocksDB and Cassandra attach one to every SSTable to avoid wasted disk reads on missing keys.

## Structure

| Parameter | Meaning |
| --- | --- |
| **`m` (bit array)** | Array of `m` bits initialized to `0` (typically ~10 bits per expected key for ~1% false positives). |
| **`k` (hash functions)** | Number of bit positions mapped per key (`k ≈ 0.693 * (m / n)`). Often simulated from 2 hashes via `h1 + i * h2`. |
| **`n` (expected items)** | Target number of keys used to size `m` and `k` upfront. |

```text
add("alice")  ──► hashes to bits 1, 5, 9:

  index:  0   1   2   3   4   5   6   7   8   9  10  11
  bits: [ 0 | 1 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 0 | 0 ]
              ▲               ▲               ▲
  might_contain("bob") ──► checks bits 1, 4, 9 ──► bit 4 is 0 ──► DEFINITELY ABSENT
```

## Operations

- **Insert (`add`):** Compute `k` hash positions in `[0, m)` and set all `k` bits to `1`. Setting a bit that is already `1` is a no-op.
- **Query (`might_contain`):** Compute the same `k` positions:
  - **Any bit is `0`:** The key was **definitely never inserted** — skip the expensive lookup.
  - **All `k` bits are `1`:** The key is **probably present** — verify against the underlying store.
- **Delete:** **Not supported** in a standard Bloom filter. Clearing a bit to `0` could break other keys sharing that bit and cause false negatives. (This fits immutable SSTables naturally; LSM deletes write tombstones to a *new* SSTable and filter.)

## Sizing & false positive rate

After `n` insertions, each bit is `0` with probability `(1 - 1/m)^(k*n) ≈ e^(-k*n/m)`. All `k` checked bits for a missing key happen to be `1` with probability:

- **False positive rate:** `p ≈ (1 - e^(-k*n/m))^k`
- **Optimal hashes (`k`):** `k = (m / n) * ln(2)` (minimizes `p` when ~50% of bits are `1`)
- **Optimal bits (`m`):** `m = -n * ln(p) / (ln 2)^2` (e.g., `p = 1%` needs `~9.6 bits/key` and `k = 7`)

## Quick comparison

| | Standard Bloom filter | Counting Bloom filter | Cuckoo / XOR filter | Hash set |
| --- | --- | --- | --- | --- |
| **Space per key** | ~10 bits (@ 1% FPR) | ~3–4× larger (counters) | ~8–12 bits (@ <1% FPR) | Full key + overhead |
| **False positives?** | Yes (tunable) | Yes (tunable) | Yes (tunable) | No |
| **Supports delete?** | No | Yes | Cuckoo: Yes · XOR: Static | Yes |

## References

- [Bloom (1970) — Space/Time Trade-offs in Hash Coding with Allowable Errors](https://dl.acm.org/doi/10.1145/362686.362692)
- [RocksDB Bloom Filter wiki](https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter)
- [Kirsch & Mitzenmacher — Less Hashing, Same Performance](https://www.eecs.harvard.edu/~michaelm/postscripts/rsa2008.pdf)

