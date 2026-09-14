# Contributing Guidelines

Thank you for contributing to the Linux Dynamic Memory Allocation & System Task Manager!

## Engineering Standards
- **Language Standards**: C11 Standard (`-std=c11`), POSIX.1-2008 compliance.
- **Compiler Flags**: All builds must pass `-Wall -Wextra -Werror -pedantic -D_GNU_SOURCE -pthread`.
- **Code Style**: Code formatting is enforced using `clang-format` (Google style, 4-space indentation).
- **Defensive Systems Programming**: Check EVERY syscall return value against `errno` (e.g., `sbrk`, `mmap`, `open`, `read`, `close`, `kill`).
- **Memory Hygiene**: Zero memory leaks allowed. Run `make valgrind` before committing.

## Commit Conventions
We enforce [Conventional Commits](https://www.conventionalcommits.org/):
- `feat:` New user-facing feature or module
- `fix:` Bug fix or security hardening
- `docs:` Documentation updates
- `test:` Unit or integration test additions
- `refactor:` Code refactoring without behavior modification
- `chore:` Build scripts, dependencies, or toolchain updates

## Pull Request Workflow
1. Fork and create a feature branch (`feat/allocator-segregated-fit`).
2. Verify all targets: `make all`, `make test`, `make asan`, `make valgrind`.
3. Submit a Pull Request targeting `main`.
