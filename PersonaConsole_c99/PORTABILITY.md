# Portability

PersonaConsole_c99 is currently a small C99/POSIX-style runtime. The core engine keeps its hot path allocation-free, but the project is not yet platform-neutral.

## Required Interfaces

- Standard C99 file I/O: `fopen`, `fread`, `fwrite`, `fflush`, `fclose`, `rename`, `remove`.
- POSIX filesystem calls: `mkdir`, `unlink`.
- POSIX sleep and timing harness support: `usleep` in the REPL, plus the engine's millisecond clock implementation.
- AETHER storage calls: `open`, `close`, `read`, `write`, `lseek`, `ftruncate`, `fsync`, `fcntl` locks.
- AETHER bucket scanning: `mmap`, `munmap`, `fstat`.
- Directory and path assumptions: forward-slash paths, cartridge-relative asset paths, and directory creation for mutable state.

## Current Binary Assumptions

- Cartridge section payloads are raw packed C structs.
- Files are written and read as little-endian/native-layout data.
- The current target is x86-class little-endian hardware.

## Planned Portability Layer

A small `platform.h` layer is planned to isolate:

- Memory-mapped file access, with a read-buffer fallback where `mmap` is unavailable.
- Directory creation and scanning.
- File locking and atomic replace behavior.
- Endianness conversion for cartridge and state files.
- Millisecond timing and sleep.

Until that layer exists, non-POSIX systems should be treated as unsupported unless they provide compatible shims for the interfaces above.
