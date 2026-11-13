# Consistent Hashing

> **Idea:** Map both servers and keys onto a shared circular hash ring so adding or removing a server only moves `~1/N` of the keys.

Naive modulo sharding (`hash(key) % N`) breaks down when cluster size `N` changes: going from 3 to 4 servers remaps ~75% of all keys at once, causing cache stampedes or massive data migrations. **Consistent hashing** decouples key placement from the exact server count and is used in DynamoDB, Cassandra, Chord, and CDN load balancers.

## Components

| Part | Role |
| --- | --- |
| **Hash ring** | Sorted circular integer space (e.g., `[0, 2^64)`) where the end wraps back to `0`. |
| **Virtual nodes (vnodes)** | Multiple ring positions (`~100–200`) assigned per physical server to smooth out partition sizes. |
| **Preference list** | First `R` *distinct physical* servers encountered clockwise from a key, used for replication. |

```text
                   0 (ring wraps around)
               Node C #1 ──●── Node A #1
                        ╱       ╲
         key "user_42" ★         ● Node B #2   ◄── "user_42" walks clockwise
         (lands here,   ╲       ╱                   to the next vnode (Node A #1)
          goes clockwise)──●───●
                     Node B #1   Node A #2
```

## Operations

- **Lookup (`get_node`):** Hash the key and binary search (`lower_bound`) the sorted ring for the first vnode position `>= hash(key)`. If past the largest position, wrap around to the first entry on the ring.
- **Add node:** Hash `"node_id#0" .. "node_id#(V-1)"` onto the ring. Each vnode steals only the keys immediately counterclockwise to it — roughly `1 / N_new` of total keys across the cluster.
- **Remove node:** Delete that server's `V` positions from the ring. Its keys automatically spill clockwise onto many different surviving servers instead of overloading a single neighbor.
- **Replication (`get_preference_list`):** Walk clockwise from `hash(key)`, collecting `R` unique physical servers (skipping duplicate vnodes belonging to a server already picked).

## Why virtual nodes matter

With only 1 ring point per server, random hash gaps create heavily skewed slices (one server might own 70% of the ring while another owns 5%). Splitting each server into `150+` vnodes:
1. Balances key load tightly around `1/N` per server.
2. Allows **heterogeneous capacity** (a 2× larger machine simply gets 2× as many vnodes).
3. Spreads recovery load evenly across the whole cluster when one node fails.

## Quick comparison

| | Modulo (`hash % N`) | Consistent hashing (vnodes) | Rendezvous (HRW) hashing | Hash slots (Redis Cluster) |
| --- | --- | --- | --- | --- |
| **Keys moved on resize** | `~(N - 1) / N` (almost all) | `~1 / N` (minimal) | `~1 / N` (minimal) | Explicit slot migration |
| **Lookup time** | `O(1)` | `O(log(N * V))` | `O(N)` per lookup | `O(1)` array lookup |
| **Ring state** | Server count `N` | Sorted array/tree of `N * V` tokens | List of `N` node IDs | Fixed table (e.g., 16,384 slots) |

## References

- [Karger et al. (1997) — Consistent Hashing and Random Trees](https://dl.acm.org/doi/10.1145/258533.258660)
- [Dynamo: Amazon’s Highly Available Key-value Store](https://www.allthingsdistributed.com/files/amazon-dynamo-sosp2007.pdf)
- [Cassandra architecture: Consistent hashing & vnodes](https://cassandra.apache.org/doc/latest/cassandra/architecture/dynamo.html)

