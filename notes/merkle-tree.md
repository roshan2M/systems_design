# Merkle Tree

> **Idea:** Build a hierarchy of hashes over data blocks so two replicas can pinpoint small differences in `O(log N)` comparisons instead of scanning everything.

A **Merkle tree** (hash tree) summarizes a sorted dataset into a single **root hash**. Every leaf hashes a data record (or key range), and every internal node hashes the concatenation of its children's hashes. Distributed databases (Cassandra, Dynamo, Riak) use Merkle trees for background **anti-entropy** repair, while Git and blockchains use them for tamper-evident content verification.

## Structure

| Node type | Contents |
| --- | --- |
| **Leaf node** | `hash("L:" + key + value)` (in practice, often the hash of a sorted bucket/sub-range of keys). |
| **Internal node** | `hash("I:" + left.hash + right.hash)` — changes automatically if *any* descendant changes. |
| **Root hash** | Single fingerprint at the top summarizing the entire dataset. |

```text
                    [ Root: H(AB + CD) ]        ◄── 1. Roots match? Done in 1 check!
                     /                \                Roots differ? Compare children:
            [ H(A + B) ]            [ H(C + D) ] ◄── 2. Left matches (skip!); Right differs
              /      \                /      \
          [H(k0)]  [H(k1)]        [H(k2)]  [H(k3)*] ◄── 3. Pinpoint mutated key k3!
```

*(Domain-separating prefixes like `"L:"` vs `"I:"` prevent second-preimage attacks where an internal node mimics a leaf.)*

## Anti-entropy sync workflow

1. **Compare roots:** Replica 1 and Replica 2 exchange root hashes. If equal, all underlying keys are identical — sync finishes in `O(1)` network traffic.
2. **Prune matching subtrees:** When two nodes have the same hash, their entire subtrees are skipped without inspecting descendants.
3. **Descend differing branches:** When hashes differ, recursively compare left and right children down to the leaves to isolate only the diverged keys or buckets (`O(d * log N)` node comparisons for `d` differences).

## Practical considerations in databases

- **Bucketed leaves:** Building a leaf per individual key across billions of keys makes the tree too large. Real engines divide a partition's key range into fixed sub-ranges (e.g., `2^15 = 32,768` leaf buckets) so tree depth is capped regardless of dataset size.
- **Aligned ranges:** Two replicas can only compare Merkle trees directly if both partition the key space into the **exact same leaf boundaries**.
- **Rebuild cost:** Computing hashes scans SSTables on disk, so anti-entropy repair runs asynchronously during compactions or off-peak windows rather than on every write.

## Quick comparison

| | Full replica checksum | Key-by-key scan / rsync | Merkle tree |
| --- | --- | --- | --- |
| **Detect if in sync** | `O(1)` transfer | `O(N)` transfer | `O(1)` transfer (root hash) |
| **Isolate 1 changed key** | Cannot localize (must rescan) | `O(N)` entries exchanged | `O(log N)` hashes exchanged |
| **Extra memory / metadata** | 1 hash | Per-block rolling/strong hashes | `~2 * B` hashes for `B` leaf buckets |

## References

- [Merkle (1987) — A Digital Signature Based on a Conventional Encryption Function](https://link.springer.com/chapter/10.1007/3-540-48184-2_32)
- [Cassandra anti-entropy repair & Merkle trees](https://cassandra.apache.org/doc/latest/cassandra/operating/repair.html)
- [Dynamo: Anti-entropy using Merkle trees (Section 4.7)](https://www.allthingsdistributed.com/files/amazon-dynamo-sosp2007.pdf)

