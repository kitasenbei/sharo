# Sharo

A minimal, Go-inspired scripting language designed specifically for game development with SDL3.

## Overview

Sharo is a bytecode-interpreted language built from the ground up for game development. Rather than being a general-purpose language with game libraries bolted on, Sharo treats SDL3 as a first-class citizen with native function integration.

```
// A simple Sharo game
init(0)
window := createWindow("My Game", 800, 600, 0)
renderer := createRenderer(window)

running := true
for running {
    event := pollEvent()
    for event != 0 {
        if event == 256 {  // QUIT
            running = false
        }
        event = pollEvent()
    }

    setDrawColor(renderer, 0, 0, 0, 255)
    clear(renderer)
    present(renderer)
    delay(16)
}

destroyRenderer(renderer)
destroyWindow(window)
quit()
```

## Features

- **Go-inspired syntax** - Clean, minimal syntax with `:=` declarations
- **Native SDL3 integration** - No FFI overhead, SDL functions are built-in
- **Bytecode VM** - Stack-based virtual machine for consistent performance
- **Closures** - First-class functions with lexical scoping
- **Garbage collection** - Mark-and-sweep GC for automatic memory management

## Requirements

- GCC or Clang (C99 compatible)
- SDL3 development libraries
- pkg-config

### Installing SDL3

**Fedora/RHEL:**
```bash
sudo dnf install SDL3-devel
```

**Ubuntu/Debian:**
```bash
# SDL3 may need to be built from source
git clone https://github.com/libsdl-org/SDL.git
cd SDL && mkdir build && cd build
cmake .. && make && sudo make install
```

**macOS:**
```bash
brew install sdl3
```

## Building

```bash
make
```

This produces the `sharo` executable in the project root.

## Usage

```bash
# Run a Sharo script
./sharo examples/game.sharo

# Run the REPL
./sharo
```

## Language Syntax

### Variables

```
// Mutable variable (type inferred)
x := 10
name := "Sharo"

// Constants
PI : 3.14159
MAX_PLAYERS : 4
```

### Functions

```
// Function declaration
add(a int, b int) int {
    return a + b
}

// Void function
greet(name str) {
    print("Hello, " + name)
}

// Lambdas
double := (x int) -> x * 2
```

### Control Flow

```
// If/else
if x > 10 {
    print("big")
} else {
    print("small")
}

// For loop (condition only)
for running {
    // game loop
}

// While loop
while count < 10 {
    count = count + 1
}
```

### Types

| Type    | Description           |
|---------|----------------------|
| `int`   | 64-bit integer       |
| `float` | 64-bit float         |
| `bool`  | Boolean              |
| `str`   | String               |
| `ptr`   | Pointer (for SDL)    |
| `nil`   | Null value           |

## SDL3 Functions

Sharo provides native bindings to SDL3 without the `SDL_` prefix:

| Sharo Function | SDL3 Equivalent |
|----------------|-----------------|
| `init(flags)` | `SDL_Init` |
| `quit()` | `SDL_Quit` |
| `createWindow(title, w, h, flags)` | `SDL_CreateWindow` |
| `destroyWindow(window)` | `SDL_DestroyWindow` |
| `createRenderer(window)` | `SDL_CreateRenderer` |
| `destroyRenderer(renderer)` | `SDL_DestroyRenderer` |
| `setDrawColor(renderer, r, g, b, a)` | `SDL_SetRenderDrawColor` |
| `clear(renderer)` | `SDL_RenderClear` |
| `present(renderer)` | `SDL_RenderPresent` |
| `fillRect(renderer, x, y, w, h)` | `SDL_RenderFillRect` |
| `pollEvent()` | `SDL_PollEvent` |
| `eventKey()` | Get scancode from event |
| `delay(ms)` | `SDL_Delay` |

See [docs/SDL3_REFERENCE.md](docs/SDL3_REFERENCE.md) for the complete SDL3 API coverage plan.

## Project Structure

```
sharolang/
├── src/
│   ├── main.c          # Entry point and REPL
│   ├── scanner.c/h     # Lexical analysis
│   ├── compiler.c/h    # Bytecode compilation
│   ├── vm.c/h          # Virtual machine
│   ├── chunk.c/h       # Bytecode chunks
│   ├── value.c/h       # Value representation
│   ├── object.c/h      # Heap objects (strings, functions)
│   ├── memory.c/h      # Memory management and GC
│   ├── table.c/h       # Hash table implementation
│   ├── debug.c/h       # Disassembler
│   └── common.h        # Common includes
├── examples/
│   ├── game.sharo      # Interactive game example
│   └── fib.sharo       # Fibonacci example
├── docs/
│   └── SDL3_REFERENCE.md
├── Makefile
└── README.md
```

## Roadmap

- [ ] Arrays and slices
- [ ] Structs/records
- [ ] Module system
- [ ] Method syntax
- [ ] Static type checking
- [ ] Complete SDL3 coverage
- [ ] Audio support
- [ ] Input handling improvements

## License

MIT License - See [LICENSE](LICENSE) for details.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
