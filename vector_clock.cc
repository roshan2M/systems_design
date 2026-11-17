#include "vector_clock.h"

#include <cassert>
#include <iostream>
#include <string>

static const char* to_str(VectorClock::Ordering ord) {
  switch (ord) {
    case VectorClock::Ordering::Equal:
      return "Equal";
    case VectorClock::Ordering::Before:
      return "Before (ancestor)";
    case VectorClock::Ordering::After:
      return "After (descendant)";
    case VectorClock::Ordering::Concurrent:
      return "Concurrent (CONFLICT!)";
  }
  return "";
}

// ============================================================================
// Walkthrough Demo
// ============================================================================
int main() {
  // 1. Causal chain: Node A writes v1, replicates to Node B, which writes v2.
  VectorClock v1;
  v1.tick("A");  // {A:1}

  VectorClock v2 = v1;
  v2.tick("B");  // {A:1, B:1}

  assert(v1.compare(v2) == VectorClock::Ordering::Before);
  assert(v2.compare(v1) == VectorClock::Ordering::After);
  assert(v1.compare(v1) == VectorClock::Ordering::Equal);

  std::cout << "1. Causal chain:\n"
            << "   v1 = " << v1.to_string() << "\n"
            << "   v2 = " << v2.to_string() << "\n"
            << "   v1 vs v2 -> " << to_str(v1.compare(v2)) << "\n\n";

  // 2. Network partition: two clients concurrently update v2 on different
  //    coordinator nodes (A and C) without seeing each other's writes.
  VectorClock branch_a = v2;
  branch_a.tick("A");  // {A:2, B:1}

  VectorClock branch_c = v2;
  branch_c.tick("C");  // {A:1, B:1, C:1}

  assert(branch_a.compare(branch_c) == VectorClock::Ordering::Concurrent);
  assert(branch_c.compare(branch_a) == VectorClock::Ordering::Concurrent);

  std::cout << "2. Partition / concurrent writes from v2:\n"
            << "   branch_a = " << branch_a.to_string() << "\n"
            << "   branch_c = " << branch_c.to_string() << "\n"
            << "   branch_a vs branch_c -> "
            << to_str(branch_a.compare(branch_c)) << "\n\n";

  // 3. Conflict resolution: client reads both siblings, merges their clocks,
  //    and writes the resolved value through coordinator B.
  VectorClock resolved = VectorClock::merged(branch_a, branch_c);
  resolved.tick("B");  // {A:2, B:2, C:1}

  assert(resolved.compare(branch_a) == VectorClock::Ordering::After);
  assert(resolved.compare(branch_c) == VectorClock::Ordering::After);

  std::cout << "3. Client resolves conflict via merge + tick(B):\n"
            << "   resolved = " << resolved.to_string() << "\n"
            << "   resolved vs branch_a -> "
            << to_str(resolved.compare(branch_a)) << "\n"
            << "   resolved vs branch_c -> "
            << to_str(resolved.compare(branch_c)) << "\n";

  return 0;
}
