# memory_allocator

A custom memory allocator written in C — building `malloc`/`free` from scratch using `mmap`.

This is a learning project exploring how low-level memory allocators work under the hood, step by step.

## Progress

- **step1_bump.c** — Reads the current program break with `sbrk(0)` to inspect the process's initial memory layout.
- **main.c** — Requests memory directly from the OS using `mmap`, instead of relying on the deprecated `sbrk`. Includes a `block_meta` header (size/free/next) placed at the start of each `mmap`'d chunk, with `block_to_ptr`/`ptr_to_block` helpers to convert between the header and the user-facing pointer, and a `make_block()` helper that mmaps a chunk and initializes its header.

  The allocator now tracks all blocks in a doubly linked list (`global_head`/`global_tail`, each `block_meta` holding both `next` and `previous`) and exposes a real `my_malloc`/`my_free` API:
  - `ALIGN16(x)` rounds a requested size up to the nearest multiple of 16, keeping every non-mmap'd allocation naturally aligned regardless of what the caller asked for.
  - `my_malloc(size)` rejects `0` and anything above `MAX_ALLOC_SIZE` (a 1 GB sanity cap, since a wrapped-negative `size_t` looks like a huge request), rounds the request up to a multiple of 16 via `ALIGN16()` (skipped for mmap'd blocks, which are already page-aligned), then calls `find_free_block()` to scan the list for a previously freed block big enough to reuse; only if none is found does it `mmap` a new block and append it to the tail of the list.
  - `find_free_block()` scans the list for the first free block with enough capacity for the request.
  - `split_block()` carves a reused free block into two: a block sized exactly to the request (marked used) and, if enough space remains (past `sizeof(block_meta) + SOME_MINIMUM`), a leftover block marked free and reinserted into the list for future reuse — avoiding wasted space when a large freed block satisfies a much smaller request. If the remainder is too small to be worth splitting, the whole block is handed over as-is and its original size is left untouched.
  - `my_calloc(count, size)` computes `count * size`, checking for multiplication overflow before allocating, calls `my_malloc()`, and zeroes the returned memory with `memset()` — the zero-initialization `malloc` doesn't provide.
  - `my_realloc(ptr, new_size)` implements the full `realloc` contract: `ptr == NULL` behaves like `my_malloc`, `new_size == 0` behaves like `my_free`, `is_mmapped` blocks skip alignment entirely, and `new_size` is otherwise rounded up via `ALIGN16()` before shrinking (reuses `split_block()` in place) or growing (tries to absorb a physically-adjacent free neighbor — `next` first, then `previous`, `memmove`-ing the data into place if it had to shift backward — before falling back to a fresh `my_malloc` + `memcpy` + `my_free` when no adjacent space is big enough).
  - `my_free(ptr)` recovers the block header from the user pointer, marks it free, and calls `coalesce()` to merge it with any adjacent free neighbors.
  - `is_physically_adjacent()` checks whether one block sits immediately after another in memory (not just in the list), which is what makes merging safe.
  - `coalesce()` merges a freed block with its `next` and/or `previous` neighbor when they are both free and physically adjacent, absorbing their size and unlinking the now-redundant node(s) — this can chain across more than two blocks, since freeing three adjacent blocks in a row folds them all into one.
  - Any request `>= MMAP_THRESHOLD` (128 KB) is handled entirely outside the free list: `my_malloc` mmaps a dedicated block and marks it `is_mmapped`, so it's never linked into `global_head`/`global_tail` and can never be split, coalesced, or reused. `my_free` checks `is_mmapped` first and `munmap`s such a block directly instead of marking it free. `my_realloc` mirrors this: growing an `is_mmapped` block always moves via a fresh `my_malloc` + `memcpy` + `my_free`, while shrinking just returns the same pointer unchanged.
  - `print_list()` walks the list and prints each block's address, size, free status, and `next` pointer, for debugging.

  `main()` exercises all of this in six phases: allocating fresh blocks, reusing a freed block (including one in the *middle* of the list), splitting a large reused block into a used piece plus a free leftover, freeing several freshly `mmap`'d adjacent blocks to show them coalesce back into one, exercising `my_calloc`/`my_realloc`'s edge cases and in-place/fallback growth paths, and finally confirming large allocations bypass the free list entirely via the mmap threshold.

  `my_realloc`'s decision flow:

  ```mermaid
  flowchart LR
      A["my_realloc(ptr, new_size)"] --> B{"new_size >\nMAX_ALLOC_SIZE?"}
      B -- yes --> R1["return NULL"]
      B -- no --> C{"ptr == NULL?"}
      C -- yes --> R2["my_malloc(new_size)"]
      C -- no --> D{"new_size == 0?"}
      D -- yes --> R3["my_free(ptr)\nreturn NULL"]
      D -- no --> E{"new_size <=\nblock->size?"}
      E -- yes --> R4["split_block()\nsame pointer, shrunk"]
      E -- no --> F{"next neighbor\nfree + adjacent\n+ big enough?"}
      F -- yes --> R5["merge next,\nsplit_block()\nsame pointer"]
      F -- no --> G{"previous neighbor\nfree + adjacent\n+ big enough?"}
      G -- yes --> R6["merge previous,\nmemmove data,\nsplit_block()\nnew pointer = previous"]
      G -- no --> R7["my_malloc(new_size)\nmemcpy + my_free(ptr)\nnew pointer"]
  ```

More steps (handling arbitrary allocation order) will be added in upcoming sessions.

## Why `mmap` instead of `sbrk`?

`sbrk` is deprecated on macOS and considered legacy even on Linux. `mmap` is the portable, modern way to request memory from the OS and is what production allocators fall back on for larger allocations.

## Building

Each step is a standalone C file for now:

```sh
clang -o main main.c
./main
```

## Testing

`test.sh` builds `main.c` and checks its output in six phases:

- **Phase 1** — confirms `my_malloc` actually allocates memory (the initial 3 blocks show up in `print_list`, marked in-use).
- **Phase 2** — confirms `find_free_block`, `my_free`, and the linked list work together: a freed block gets reused (including a block in the *middle* of the list), and `print_list` reflects head/tail pointers and free/used status correctly.
- **Phase 3** — confirms `split_block` actually carves a reused block in two: the list grows a new node after the split, and the dump shows the split "signature" — a small used block immediately followed by a larger free leftover block.
- **Phase 4** — confirms `coalesce` merges adjacent free blocks: after freeing several freshly allocated, physically-adjacent blocks, the node count doesn't grow, no two consecutive list entries are left free and unmerged, and the resulting free block's size reflects multiple blocks being absorbed into one.
- **Phase 5** — confirms `my_calloc` zero-initializes its returned memory and correctly rejects both a `count * size` multiplication overflow and a `size == 0` request; and confirms `my_realloc` handles `NULL`/`0` like `malloc`/`free`, shrinks in place, grows in place by merging an adjacent free neighbor, and falls back to allocate+copy+free (preserving the original data) when no adjacent space is available.
- **Phase 6** — confirms the mmap threshold: a request `>= MMAP_THRESHOLD` is marked `is_mmapped` and excluded from `print_list`'s walk entirely, while a request just below the threshold is not; `my_free` munmaps an `is_mmapped` block instead of returning it to the free list; and `my_realloc` on an `is_mmapped` block always moves (grow) or returns the same pointer unchanged (shrink), preserving data across the move.

```sh
./test.sh
```

## Requirements

- A C compiler (`clang` or `gcc`)
- POSIX-compliant OS (macOS/Linux)
