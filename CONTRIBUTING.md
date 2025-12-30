# Contributing to Sharo

Thank you for your interest in contributing to Sharo! This document provides guidelines and information for contributors.

## Branch Strategy

We use a modified Git Flow workflow:

- **`main`** - Stable releases only. Protected branch.
- **`develop`** - Integration branch for features. All PRs target this branch.
- **`feature/*`** - Feature branches (e.g., `feature/arrays`, `feature/structs`)
- **`bugfix/*`** - Bug fix branches (e.g., `bugfix/scanner-escape`)
- **`release/*`** - Release preparation branches (e.g., `release/0.2.0`)
- **`hotfix/*`** - Critical fixes for production (e.g., `hotfix/memory-leak`)

### Workflow

1. Create a feature branch from `develop`:
   ```bash
   git checkout develop
   git pull origin develop
   git checkout -b feature/my-feature
   ```

2. Make your changes with clear, atomic commits

3. Push and create a Pull Request targeting `develop`:
   ```bash
   git push -u origin feature/my-feature
   ```

4. After review and CI passes, the PR will be merged

## Commit Messages

Follow conventional commit format:

```
type(scope): short description

Longer description if needed.
```

**Types:**
- `feat` - New feature
- `fix` - Bug fix
- `docs` - Documentation changes
- `refactor` - Code refactoring
- `test` - Adding or updating tests
- `chore` - Build, CI, or tooling changes

**Examples:**
```
feat(vm): add array indexing support
fix(scanner): handle escaped quotes in strings
docs(readme): update SDL3 installation instructions
refactor(compiler): extract expression parsing into separate functions
```

## Code Style

### C Code Guidelines

- **Standard:** C99
- **Indentation:** 4 spaces (no tabs)
- **Line length:** 100 characters max
- **Braces:** K&R style
- **Naming:**
  - Functions: `camelCase` (e.g., `initVM`, `scanToken`)
  - Types: `PascalCase` (e.g., `Value`, `ObjFunction`)
  - Macros: `UPPER_SNAKE_CASE` (e.g., `AS_INT`, `IS_BOOL`)
  - Local variables: `camelCase`

### Example

```c
static Value addValues(Value a, Value b) {
    if (IS_INT(a) && IS_INT(b)) {
        return INT_VAL(AS_INT(a) + AS_INT(b));
    }

    if (IS_FLOAT(a) && IS_FLOAT(b)) {
        return FLOAT_VAL(AS_FLOAT(a) + AS_FLOAT(b));
    }

    runtimeError("Operands must be numbers.");
    return NIL_VAL;
}
```

## Building and Testing

### Build

```bash
make clean
make
```

### Run Examples

```bash
./sharo examples/fib.sharo
./sharo examples/game.sharo
```

### Debug Build

```bash
make DEBUG=1
```

## Adding SDL3 Functions

When adding new SDL3 native functions:

1. Add the native function in `src/vm.c`:
   ```c
   static Value myFunctionNative(int argCount, Value* args) {
       // Implementation
   }
   ```

2. Register it in `initVM()`:
   ```c
   defineNative("myFunction", myFunctionNative);
   ```

3. Update `docs/SDL3_REFERENCE.md` to mark it as implemented

4. Add an example demonstrating its use

## Pull Request Checklist

Before submitting a PR:

- [ ] Code compiles without warnings (`-Wall -Wextra`)
- [ ] Existing examples still work
- [ ] New features have example code
- [ ] Commit messages follow conventions
- [ ] PR description explains the changes
- [ ] Documentation updated if needed

## Reporting Issues

When reporting bugs, include:

1. Sharo version (git commit hash)
2. Operating system and version
3. SDL3 version
4. Minimal code to reproduce the issue
5. Expected vs actual behavior

## Feature Requests

For feature requests, please:

1. Check existing issues first
2. Describe the use case
3. Provide example syntax if proposing language changes
4. Explain how it fits Sharo's game development focus

## Architecture Overview

```
Source Code (.sharo)
       │
       ▼
   ┌─────────┐
   │ Scanner │  Lexical analysis → Tokens
   └─────────┘
       │
       ▼
   ┌──────────┐
   │ Compiler │  Parsing → Bytecode
   └──────────┘
       │
       ▼
   ┌─────┐
   │ VM  │  Execution
   └─────┘
```

Key files:
- `scanner.c` - Tokenization
- `compiler.c` - Pratt parser, bytecode generation
- `vm.c` - Stack-based bytecode interpreter, SDL natives
- `object.c` - Heap-allocated objects (strings, functions, closures)
- `memory.c` - Allocation and garbage collection

## Questions?

Open an issue with the `question` label or start a discussion.
