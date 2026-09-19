#!/usr/bin/env bash
# Test script for the memory_allocator project.
# Builds main.c, runs it, and checks the output in phases.

set -u

PASS=0
FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

echo "=== Building ==="
clang -Wall -Wextra -o main main.c
if [ $? -ne 0 ]; then
    echo "Build failed."
    exit 1
fi
echo "Build OK."
echo

OUTPUT=$(./main)
echo "$OUTPUT"
echo

echo "=== Phase 1: my_malloc allocates memory ==="
# main() calls my_malloc 3 times (p1, p2, p3) before the FIRST print_list() call.
# Grab only that first list block (up to the first "head=" line).

FIRST_LIST=$(echo "$OUTPUT" | awk '/--- list ---/{f=1} f{print} f && /^head=/{exit}')

BLOCK_COUNT=$(echo "$FIRST_LIST" | grep -c '^\s*\[')

if [ "$BLOCK_COUNT" -eq 3 ]; then
    pass "my_malloc created 3 blocks (p1, p2, p3)"
else
    fail "expected 3 blocks after initial my_malloc calls, got $BLOCK_COUNT"
fi

if [ -n "$FIRST_LIST" ]; then
    pass "print_list produced non-empty output after my_malloc calls"
else
    fail "print_list produced no output"
fi

if echo "$FIRST_LIST" | grep -q "free=0"; then
    pass "allocated blocks are marked free=0 (in use)"
else
    fail "expected allocated blocks marked free=0"
fi

echo
echo "=== Phase 2: find_free_block / my_free / print_list (linked list reuse) ==="

if echo "$OUTPUT" | grep -q "a==b? YES"; then
    pass "my_free + find_free_block: freed block reused (a==b)"
else
    fail "freed block was not reused (a==b check failed)"
fi

if echo "$OUTPUT" | grep -q "middle-block reuse: reused==m3? YES"; then
    pass "find_free_block scans the full list and reuses a middle block"
else
    fail "middle-block reuse failed (linked list traversal or free marking bug)"
fi

if echo "$OUTPUT" | grep -q "head=0x" && echo "$OUTPUT" | grep -q "tail=0x"; then
    pass "print_list reports head/tail pointers (linked list constructed)"
else
    fail "print_list did not report head/tail pointers"
fi

FREE_COUNT=$(echo "$OUTPUT" | grep -c "free=1")
if [ "$FREE_COUNT" -ge 1 ]; then
    pass "print_list shows at least one block marked free=1 after my_free"
else
    fail "expected at least one free=1 block after my_free calls"
fi

echo
echo "=== Phase 3: split_block (reused block gets carved into used + leftover) ==="

# Grab the SECOND print_list() dump (after the m1..m5 + my_free(m3) + reused = my_malloc(30) sequence).
SECOND_LIST=$(echo "$OUTPUT" | awk '/--- list ---/{n++} n==2{print} n==2 && /^head=/{exit}')

if [ -n "$SECOND_LIST" ]; then
    pass "print_list produced output for the split scenario"
else
    fail "print_list produced no output for the split scenario"
fi

# By this point in main(), 8 my_malloc calls have been made (p1,p2,p3,a,m1,m2,m4,m5)
# that were never freed and stayed as their own blocks, plus m3 was freed and reused.
# If split_block works, reusing m3 for the 30-byte request must leave a NEW leftover
# free block behind -- so the list must have grown to MORE than 8 nodes.
SECOND_BLOCK_COUNT=$(echo "$SECOND_LIST" | grep -c '^\s*\[')

if [ "$SECOND_BLOCK_COUNT" -gt 8 ]; then
    pass "list grew to $SECOND_BLOCK_COUNT nodes -- split_block created a leftover node"
else
    fail "expected more than 8 nodes after split (leftover block missing), got $SECOND_BLOCK_COUNT"
fi

# The real signature of a correct split: a small, used (free=0) block -- the exact-
# fit piece carved out and handed to the caller -- immediately followed by a larger
# free=1 leftover block (the remainder split_block put back in the list for reuse).
SPLIT_SIGNATURE=$(echo "$SECOND_LIST" | awk '
    function get_size(line,    n) {
        n = split(line, parts, "size=")
        split(parts[2], rest, " ")
        return rest[1] + 0
    }
    /free=0/ {
        used_size = get_size($0)
        getline nextline
        if (nextline ~ /free=1/) {
            leftover_size = get_size(nextline)
            if (used_size < leftover_size) { print "found"; exit }
        }
    }
')

if [ "$SPLIT_SIGNATURE" = "found" ]; then
    pass "found split signature: small used block immediately followed by a larger free leftover block"
else
    fail "no split signature found (used block -> larger free leftover adjacency missing)"
fi

echo
echo "=== Phase 4: coalesce merges adjacent free blocks ==="

# Grab the THIRD print_list() dump (after q1,q2,q3 are freed).
THIRD_LIST=$(echo "$OUTPUT" | awk '/--- list ---/{n++} n==3{print} n==3 && /^head=/{exit}')

if [ -n "$THIRD_LIST" ]; then
    pass "print_list produced output for the coalesce scenario"
else
    fail "print_list produced no output for the coalesce scenario"
fi

# q1, q2, q3 are all freed right after being allocated. If coalesce() works, they
# merge with each other (and with the pre-existing free tail block) into ONE node,
# instead of leaving 3 separate free=1 nodes sitting in the list.
THIRD_BLOCK_COUNT=$(echo "$THIRD_LIST" | grep -c '^\s*\[')
SECOND_BLOCK_COUNT_FOR_PHASE4=$(echo "$SECOND_LIST" | grep -c '^\s*\[')

if [ "$THIRD_BLOCK_COUNT" -le "$SECOND_BLOCK_COUNT_FOR_PHASE4" ]; then
    pass "node count did not grow after freeing q1/q2/q3 ($SECOND_BLOCK_COUNT_FOR_PHASE4 -> $THIRD_BLOCK_COUNT nodes) -- adjacent free blocks merged"
else
    fail "node count grew after freeing q1/q2/q3 ($SECOND_BLOCK_COUNT_FOR_PHASE4 -> $THIRD_BLOCK_COUNT) -- coalesce did not merge them"
fi

# There should be no more than one free=1 node left dangling as separate, unmerged
# neighbors -- i.e. no two CONSECUTIVE list entries should both be free=1, since a
# working coalesce() would have merged any such pair into a single node already.
CONSECUTIVE_FREE_PAIR=$(echo "$THIRD_LIST" | awk '
    /free=1/ {
        getline nextline
        if (nextline ~ /free=1/) { print "found"; exit }
    }
')

if [ "$CONSECUTIVE_FREE_PAIR" != "found" ]; then
    pass "no two consecutive free blocks left unmerged in the list"
else
    fail "found two consecutive free=1 blocks -- coalesce failed to merge them"
fi

# The final free block's size should reflect the merge: it must be strictly larger
# than any single freshly-mmap'd block would be on its own (i.e. bigger than what a
# lone my_malloc(20000) chunk would produce), proving multiple blocks were absorbed.
LAST_FREE_SIZE=$(echo "$THIRD_LIST" | awk '/free=1/ { match($0, /size=[0-9]+/); s = substr($0, RSTART+5, RLENGTH-5); last = s } END { print last+0 }')

if [ "$LAST_FREE_SIZE" -gt 32736 ]; then
    pass "merged free block size ($LAST_FREE_SIZE) exceeds a single fresh block's size -- multiple blocks absorbed"
else
    fail "merged free block size ($LAST_FREE_SIZE) too small -- coalesce did not absorb multiple blocks"
fi

echo
echo "=== Summary ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
