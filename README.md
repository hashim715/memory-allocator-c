# memory_allocator

A custom memory allocator written in C — building `malloc`/`free` from scratch using `mmap`.

This is a learning project exploring how low-level memory allocators work under the hood, step by step.

## Progress

- **step1_bump.c** — Reads the current program break with `sbrk(0)` to inspect the process's initial memory layout.
- **main.c** — Requests memory directly from the OS using `mmap`, instead of relying on the deprecated `sbrk`. Now includes a `block_meta` header (size/free/next) placed at the start of each `mmap`'d chunk, with `block_to_ptr`/`ptr_to_block` helpers to convert between the header and the user-facing pointer, and a `make_block()` helper that mmaps a chunk and initializes its header. The program allocates a few blocks and logs header sizes, pointer round-tripping, and alignment to verify the layout is correct.

More steps (free lists, chunk headers, splitting/coalescing, a real `malloc`/`free` API) will be added in upcoming sessions.

## Why `mmap` instead of `sbrk`?

`sbrk` is deprecated on macOS and considered legacy even on Linux. `mmap` is the portable, modern way to request memory from the OS and is what production allocators fall back on for larger allocations.

## Building

Each step is a standalone C file for now:

```sh
clang -o main main.c
./main
```

## Requirements

- A C compiler (`clang` or `gcc`)
- POSIX-compliant OS (macOS/Linux)
