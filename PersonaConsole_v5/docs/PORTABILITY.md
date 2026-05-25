# Portability

PersonaConsole_c99 is currently a small C99/POSIX-style runtime. The core engine keeps its hot path allocation-free, but the project is not yet platform-neutral.

## Supported Build Platforms

The runtime is built with the same toolchain on both supported systems:

- **Linux** — native `gcc`/`cc`. `make`, `make host`, `make v4_all_tests`.
- **Windows** — the **Cygwin** `gcc` toolchain (run `make` from a Cygwin
  shell; the PowerShell release scripts add `C:\cygwin64\bin` to `PATH`).
  Cygwin supplies the POSIX interfaces listed below (sockets, `mmap`,
  `fcntl` locks, `usleep`), so no `_WIN32` code paths are required and the
  sources contain none. The resulting `persona_host.exe` ships with the
  Cygwin runtime DLL in the demo package.

Native MSVC is **not** a target (it lacks the POSIX layer). macOS works as a
POSIX system where the interfaces below are available.

### Line endings

All text is stored **LF** in the repository and checked out LF on every
platform; this is enforced by the top-level `.gitattributes` (with
`.editorconfig` as editor-level backup). Windows shell tooling (`*.ps1`,
`*.cmd`, `*.bat`) is the only exception and is checked out CRLF. Cartridge and
model artifacts (`*.bin`, `*.cart`, `*.lm`) are marked `binary` so end-of-line
conversion can never corrupt their packed-struct ABI. Keeping endings
consistent is what allows the same source tree to build byte-identically on
Linux and Windows and avoids spurious whole-file diffs between contributors.

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
- Cartridge struct capacities are part of the binary ABI. The modern
  low-resource standard raises dialogue capacity to 1024 templates,
  256 patterns, 256-character template text, and 20 core seed memories.
  Existing cartridges must be regenerated whenever these limits change.

## Planned Portability Layer

A small `platform.h` layer is planned to isolate:

- Memory-mapped file access, with a read-buffer fallback where `mmap` is unavailable.
- Directory creation and scanning.
- File locking and atomic replace behavior.
- Endianness conversion for cartridge and state files.
- Millisecond timing and sleep.

Until that layer exists, non-POSIX systems should be treated as unsupported unless they provide compatible shims for the interfaces above.
