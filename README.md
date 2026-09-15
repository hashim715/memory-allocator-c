# memory_allocator

A custom memory allocator written in C — building `malloc`/`free` from scratch using `mmap`.

This is a learning project exploring how low-level memory allocators work under the hood, step by step.

## Progress

- **step1_bump.c** — Reads the current program break with `sbrk(0)` to inspect the process's initial memory layout.
- **main.c** — Requests memory directly from the OS using `mmap`, instead of relying on the deprecated `sbrk`. Includes a `block_meta` header (size/free/next) placed at the start of each `mmap`'d chunk, with `block_to_ptr`/`ptr_to_block` helpers to convert between the header and the user-facing pointer, and a `make_block()` helper that mmaps a chunk and initializes its header.

  The allocator now tracks all blocks in a singly linked list (`global_head`/`global_tail`) and exposes a real `my_malloc`/`my_free` API:
  - `my_malloc(size)` rejects `0` and anything above `MAX_ALLOC_SIZE` (a 1 GB sanity cap, since a wrapped-negative `size_t` looks like a huge request), then calls `find_free_block()` to scan the list for a previously freed block big enough to reuse; only if none is found does it `mmap` a new block and append it to the tail of the list.
  - `find_free_block()` scans the list for the first free block with enough capacity for the request.
  - `split_block()` carves a reused free block into two: a block sized exactly to the request (marked used) and, if enough space remains (past `sizeof(block_meta) + SOME_MINIMUM`), a leftover block marked free and reinserted into the list for future reuse — avoiding wasted space when a large freed block satisfies a much smaller request.
  - `my_free(ptr)` recovers the block header from the user pointer and marks it free, making it available for reuse by a later `my_malloc` call.
  - `print_list()` walks the list and prints each block's address, size, free status, and `next` pointer, for debugging.

  `main()` exercises this: it allocates a few blocks, frees one and confirms a smaller subsequent allocation reuses that same freed block (including a block in the *middle* of the list, not just the most recently freed one).

More steps (coalescing adjacent freed blocks, handling arbitrary allocation order, a real `realloc`) will be added in upcoming sessions.

## Why `mmap` instead of `sbrk`?

`sbrk` is deprecated on macOS and considered legacy even on Linux. `mmap` is the portable, modern way to request memory from the OS and is what production allocators fall back on for larger allocations.

## Building

Each step is a standalone C file for now:

```sh
clang -o main main.c
./main
```

## Testing

`test.sh` builds `main.c` and checks its output in two phases:

- **Phase 1** — confirms `my_malloc` actually allocates memory (the initial 3 blocks show up in `print_list`, marked in-use).
- **Phase 2** — confirms `find_free_block`, `my_free`, and the linked list work together: a freed block gets reused (including a block in the *middle* of the list), and `print_list` reflects head/tail pointers and free/used status correctly.
- **Phase 3** — confirms `split_block` actually carves a reused block in two: the list grows a new node after the split, and the dump shows the split "signature" — a free leftover block immediately followed by a smaller used block.

```sh
./test.sh
```

## Requirements

- A C compiler (`clang` or `gcc`)
- POSIX-compliant OS (macOS/Linux)
