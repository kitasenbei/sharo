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
│   ├── object.c/h      # Heap objects (strings, functions, closures, arrays, structs)
│   ├── table.c/h       # Hash table for globals and string interning
│   ├── memory.c/h      # Memory allocation, GC
│   ├── debug.c/h       # Bytecode disassembler
│   └── common.h        # Common includes and defines
├── assets/
│   └── cow.bmp         # Sprite for benchmark
├── examples/
│   ├── arrays.sharo    # Array feature tests
│   ├── structs.sharo   # Struct feature tests
│   ├── methods.sharo   # Method feature tests
│   ├── modules/        # Import system demo
│   ├── tictactoe.sharo # Playable SDL3 game
│   ├── cowmark.sharo   # Sprite benchmark
│   └── test.sharo      # Language feature tests
├── Makefile
└── CLAUDE.md           # This file
```

## Language Syntax

### Variables
```
x := 10              // Mutable, type inferred
MAX : 100            // Constant
```

### Functions
```
add(a int, b int) int {
    return a + b
}

greet() {
    print("Hello!")
}

// Lambdas
double := (x int) -> x * 2
```

### Arrays
```
arr := [1, 2, 3]      // Array literal
arr[0]                // Index access (0)
arr[1] = 10           // Index assignment
len(arr)              // Length (3)
push(arr, 4)          // Append, returns new length
pop(arr)              // Remove and return last element
```

### Structs
```
type Point {
    x: int,
    y: int
}

p := Point(10, 20)    // Constructor (positional args)
print(p.x)            // Field access
p.y = 30              // Field assignment
```

### Methods
```
type Counter {
    value: int

    increment() {
        self.value = self.value + 1
    }

    get() int {
        return self.value
    }
}

c := Counter(0)
c.increment()
print(c.get())        // 1
```

### Modules
```
// math.sharo
PI := 3.14159
square(n int) int { return n * n }

// main.sharo
import "math.sharo"
print(PI)             // 3.14159
print(square(5))      // 25
```

### Control Flow
```
if condition {
    // ...
} else {
    // ...
}

for condition {
    // while-style loop
}
```

## Building

```bash
make          # Build with SDL3 + SDL3_ttf
make clean    # Clean build
./sharo       # REPL
./sharo file.sharo  # Run file
```

Requires SDL3 and SDL3_ttf development libraries.

## Value Types

| Type | C Representation | Sharo Literal |
|------|-----------------|---------------|
| int | int64_t | `42`, `0xFF`, `0b1010` |
| float | double | `3.14`, `1e-10` |
| bool | bool | `true`, `false` |
| nil | - | `nil` |
| str | ObjString* | `"hello"` |
| ptr | void* | (from SDL functions) |
| array | ObjArray* | `[1, 2, 3]` |
| struct | ObjStruct* | `Point(10, 20)` |

## Object Types (object.h)

| Type | Description |
|------|-------------|
| OBJ_STRING | Interned strings |
| OBJ_FUNCTION | Compiled functions |
| OBJ_CLOSURE | Functions with captured upvalues |
| OBJ_NATIVE | Built-in C functions |
| OBJ_UPVALUE | Captured variables |
| OBJ_ARRAY | Dynamic arrays |
| OBJ_STRUCT_DEF | Struct type definition |
| OBJ_STRUCT | Struct instance |
| OBJ_BOUND_METHOD | Method bound to instance |

## Native Functions

### Core
- `clock()` - CPU time in seconds
- `print(value)` - Print to stdout
- `len(arr|str)` - Length of array or string
- `push(arr, val)` - Append to array
- `pop(arr)` - Remove last from array

### SDL3 - Initialization
- `init(flags)` - Initialize SDL
- `quit()` - Shutdown SDL

### SDL3 - Window
- `createWindow(title, w, h, flags)` - Create window, returns ptr
- `destroyWindow(window)` - Destroy window

### SDL3 - Renderer
- `createRenderer(window)` - Create renderer, returns ptr
- `destroyRenderer(renderer)` - Destroy renderer
- `clear(renderer)` - Clear screen
- `present(renderer)` - Swap buffers
- `setDrawColor(renderer, r, g, b, a)` - Set draw color
- `fillRect(renderer, x, y, w, h)` - Draw filled rectangle
- `drawRect(renderer, x, y, w, h)` - Draw rectangle outline

### SDL3 - Textures
- `loadTexture(renderer, path)` - Load BMP image, returns ptr
- `destroyTexture(texture)` - Free texture
- `drawTexture(renderer, texture, x, y, w, h)` - Draw texture
- `getTextureSize(texture)` - Returns [width, height] array

### SDL3 - Events
- `pollEvent()` - Poll event, returns event type (int)
- `eventKey()` - Get key scancode from last event

### SDL3 - Time
- `delay(ms)` - Sleep for milliseconds
- `getTicks()` - Milliseconds since init

### SDL3 - Random
- `random(max)` - Random int from 0 to max-1
- `randomFloat()` - Random float from 0.0 to 1.0

### SDL3_ttf - Text
- `initTTF()` - Initialize TTF subsystem
- `quitTTF()` - Shutdown TTF
- `loadFont(path, size)` - Load TTF font, returns ptr
- `destroyFont(font)` - Free font
- `drawText(renderer, font, text, x, y, r, g, b)` - Render text

## SDL Constants

```
SDL_INIT_VIDEO := 0x00000020
SDL_EVENT_QUIT := 256
SDL_EVENT_KEY_DOWN := 768

// Key scancodes (SDL3)
KEY_ESC := 41
KEY_SPACE := 44
KEY_1 := 30  // through KEY_9 := 38
KEY_Q := 20
KEY_R := 21
// etc.
```

## Adding SDL Functions

1. In `vm.c`, add the native function:
```c
static Value myFunctionNative(int argCount, Value* args) {
    (void)argCount;
    int x = (int)AS_INT(args[0]);
    SDL_MyFunction(x);
    return NIL_VAL;  // or BOOL_VAL(), INT_VAL(), PTR_VAL(), etc.
}
```

2. Register it in `initVM()`:
```c
defineNative("myFunction", myFunctionNative);
```

## Adding Language Features

1. Add tokens to `scanner.h` and `scanner.c`
2. Add parsing to `compiler.c` (Pratt parser for expressions)
3. Add opcodes to `chunk.h` if needed
4. Add VM execution in `vm.c`
5. Add object types to `object.h/c` if needed
6. Update GC marking in `memory.c`

## Key Design Decisions

1. **SDL is the standard library** - No generic FFI, SDL functions are built-in natives
2. **Bare parens function syntax** - `add(x int) int { }` not `fn add(...)`
3. **No semicolons** - Newline-terminated statements
4. **`:=` for declaration, `=` for assignment**
5. **Value types include `ptr`** - For SDL handles (windows, renderers, etc.)
6. **Constructor syntax** - `Point(x, y)` not `Point{x: 1, y: 2}`
7. **`self` keyword** - Used in methods to access instance

## Debugging

Enable in `common.h`:
- `DEBUG_TRACE_EXECUTION` - Print bytecode as it executes
- `DEBUG_PRINT_CODE` - Print compiled bytecode

## Performance

Cowmark benchmark: ~10,000 sprites @ 50 FPS
- Bottleneck is VM bytecode interpretation for per-sprite updates
- SDL3 rendering is GPU-accelerated
