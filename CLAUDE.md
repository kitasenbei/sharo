# Sharo Language - Project Guide

Sharo is a minimal, game-focused scripting language with SDL3 as its native backend.

## Project Structure

```
sharolang/
├── src/
│   ├── main.c          # Entry point, REPL, file runner
│   ├── scanner.c/h     # Lexer/tokenizer
│   ├── compiler.c/h    # Pratt parser, bytecode compiler
│   ├── vm.c/h          # Stack-based virtual machine + SDL natives
│   ├── chunk.c/h       # Bytecode container
│   ├── value.c/h       # Value representation (int64, float64, bool, nil, ptr, obj)
│   ├── object.c/h      # Heap objects (strings, functions, closures)
│   ├── table.c/h       # Hash table for globals and string interning
│   ├── memory.c/h      # Memory allocation, GC
│   ├── debug.c/h       # Bytecode disassembler
│   └── common.h        # Common includes and defines
├── examples/
│   ├── game.sharo      # Interactive game demo
│   ├── fib.sharo       # Fibonacci example
│   └── test.sharo      # Language feature tests
├── docs/
│   └── SDL3_REFERENCE.md  # Complete SDL3 API to implement
├── Makefile
└── CLAUDE.md           # This file
```

## Language Syntax

```
// Variables
x := 10              // Mutable, type inferred
MAX : 100            // Constant

// Functions (bare parens syntax)
add(a int, b int) int {
    return a + b
}

greet() {
    print("Hello!")
}

// Lambdas
double := (x int) -> x * 2

// Control flow
if condition {
    // ...
} else {
    // ...
}

for condition {
    // while-style loop
}

// SDL usage (no prefix)
init(0)
window := createWindow("Game", 800, 600, 0)
renderer := createRenderer(window)
```

## Building

```bash
make          # Build with SDL3
make clean    # Clean build
./sharo       # REPL
./sharo file.sharo  # Run file
```

Requires SDL3 development libraries (`pkg-config --libs sdl3`).

## Value Types

| Type | C Representation | Sharo Literal |
|------|-----------------|---------------|
| int | int64_t | `42`, `0xFF`, `0b1010` |
| float | double | `3.14`, `1e-10` |
| bool | bool | `true`, `false` |
| nil | - | `nil` |
| str | ObjString* | `"hello"` |
| ptr | void* | (from SDL functions) |

## Opcodes (chunk.h)

Key opcodes for reference:
- `OP_CONSTANT`, `OP_NIL`, `OP_TRUE`, `OP_FALSE`
- `OP_ADD`, `OP_SUBTRACT`, `OP_MULTIPLY`, `OP_DIVIDE`, `OP_MODULO`, `OP_NEGATE`
- `OP_EQUAL`, `OP_NOT_EQUAL`, `OP_GREATER`, `OP_LESS`, etc.
- `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_LOOP`
- `OP_CALL`, `OP_CLOSURE`, `OP_RETURN`
- `OP_GET_GLOBAL`, `OP_SET_GLOBAL`, `OP_DEFINE_GLOBAL`
- `OP_GET_LOCAL`, `OP_SET_LOCAL`
- `OP_GET_UPVALUE`, `OP_SET_UPVALUE`, `OP_CLOSE_UPVALUE`
- `OP_PRINT`

## Adding SDL Functions

To add a new SDL function as a native:

1. In `vm.c`, add the native function:
```c
static Value myFunctionNative(int argCount, Value* args) {
    (void)argCount;
    // Extract args
    int x = (int)AS_INT(args[0]);
    // Call SDL
    SDL_MyFunction(x);
    // Return result
    return NIL_VAL;  // or BOOL_VAL(), INT_VAL(), PTR_VAL(), etc.
}
```

2. Register it in `initVM()`:
```c
defineNative("myFunction", myFunctionNative);
```

3. Update `docs/SDL3_REFERENCE.md` to mark as implemented.

## SDL3 Implementation Status

See `docs/SDL3_REFERENCE.md` for complete API reference.

Currently implemented:
- Initialization: `init`, `quit`
- Window: `createWindow`, `destroyWindow`
- Renderer: `createRenderer`, `destroyRenderer`, `clear`, `present`
- Drawing: `setDrawColor`, `fillRect`, `drawRect`
- Events: `pollEvent`, `eventKey`
- Time: `delay`, `getTicks`

## Key Design Decisions

1. **SDL is the standard library** - No generic FFI, SDL functions are built-in natives
2. **Bare parens function syntax** - `add(x int) int { }` not `fn add(...)`
3. **No semicolons** - Newline-terminated statements
4. **`:=` for declaration, `=` for assignment**
5. **Value types include `ptr`** - For SDL handles (windows, renderers, etc.)

## Common Tasks

### Run the game demo
```bash
./sharo examples/game.sharo
```

### Test language features
```bash
./sharo examples/test.sharo
```

### Debug bytecode
Enable `DEBUG_TRACE_EXECUTION` in `common.h` to see bytecode execution.

### Add a new language feature
1. Add tokens to `scanner.h` and `scanner.c`
2. Add parsing to `compiler.c` (use Pratt parser for expressions)
3. Add opcodes to `chunk.h` if needed
4. Add VM execution in `vm.c`
