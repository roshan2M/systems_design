# Vector Clocks

> **Idea:** Track a logical counter per node `{node -> count}` on every version so replicas can tell whether one update *caused* another or whether two updates conflicted concurrently.

In distributed systems without a single leader, physical wall-clock timestamps ("last-write-wins") can silently drop data because server clocks drift out of sync. **Vector clocks** capture **causal history** without relying on synchronized clocks, allowing leaderless databases (Dynamo, Riak, Voldemort) to preserve concurrent conflicting writes ("siblings") for safe resolution.

## Structure & rules

A vector clock is a map from node/actor ID to an integer counter, carried alongside each value as metadata:

1. **Local write at node `X` (`tick`):** Increment `clock[X] += 1`.
2. **Send / store:** Attach the updated vector clock to the value or message.
3. **Receive / reconcile (`merge`):** Take the element-wise maximum across all nodes (`merged[n] = max(A[n], B[n])`), then `tick` the coordinating node when writing the new version.

```text
       v1 {A:1}                 1. Client writes v1 via Node A
          │
       v2 {A:1, B:1}            2. Client reads v1, updates to v2 via Node B (v1 -> v2)
        ╱           ╲
branch_a             branch_c   3. Concurrent updates from v2 during a partition!
{A:2, B:1}           {A:1, B:1, C:1}   ──► Neither <= the other: CONFLICT (siblings)
        ╲           ╱
      resolved {A:2, B:2, C:1}  4. Client reads both siblings, merges clocks, writes via B
```

## Comparing two vector clocks (`A` vs `B`)

Treat missing node entries as `0`:

| Outcome | Condition across all nodes `n` | Meaning |
| --- | --- | --- |
| **`Equal`** | `A[n] == B[n]` | Exact same version. |
| **`Before` (`A -> B`)** | `A[n] <= B[n]` for all `n`, and `A != B` | `A` is a causal ancestor of `B`; `B` safely overwrites `A`. |
| **`After` (`B -> A`)** | `B[n] <= A[n]` for all `n`, and `A != B` | `A` causally supersedes `B`; `A` safely overwrites `B`. |
| **`Concurrent` (`A \|\| B`)** | `A` is larger on some node(s) *and* `B` is larger on other(s) | Neither saw the other write — **conflict** must be kept/merged! |

## Conflict resolution & practical caveats

- **Resolving siblings:** Vector clocks *detect* conflicts; they do not merge application data by themselves. Resolution is done by the client (e.g., unioning a shopping cart), by a **CRDT** (conflict-free replicated data type), or by falling back to a tie-breaker.
- **Actor bloat & pruning:** If client IDs (rather than server IDs) are used as keys, clocks grow unboundedly. Bounding size by pruning oldest entries can reintroduce false conflicts; **dotted version vectors** (used in Riak 2.0) track server-centric causality more compactly without false concurrency from client retries.

## Quick comparison

| | Wall-clock LWW | Lamport timestamps | Vector clocks | Dotted version vectors |
| --- | --- | --- | --- | --- |
| **Metadata size** | 1 timestamp (64-bit) | 1 `(counter, node)` pair | `O(nodes)` counters | `O(replicas)` counters + dot |
| **Detects concurrent conflicts?** | No (drops earlier clock) | No (forces arbitrary total order) | **Yes** (`A -> B` iff `V(A) < V(B)`) | **Yes** (handles client concurrency cleanly) |

## References

- [Lamport (1978) — Time, Clocks, and the Ordering of Events in a Distributed System](https://lamport.azurewebsites.net/pubs/time-clocks.pdf)
- [Fidge (1988) / Mattern (1989) — Timestamps in Message-Passing Systems / Virtual Time](https://fileadmin.cs.lth.se/cs/Personal/Amr_Ergawy/dist-algos-papers/4.pdf)
- [Riak docs: Vector Clocks & Dotted Version Vectors](https://docs.riak.com/riak/kv/latest/learn/concepts/causal-context/)

