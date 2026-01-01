# Sharo

A minimal scripting language for game development with SDL3.

## Quick Start

```bash
make
./sharo examples/sharoui_demo.sharo
```

## Syntax

```
// Variables
x := 10
PI : 3.14159

// Functions
add(a int, b int) int {
    return a + b
}

// Lambdas
double := (x int) -> x * 2

// Arrays
items := [1, 2, 3]
push(items, 4)

// Structs
type Point { x: int, y: int }
p := Point(10, 20)

// Methods
type Counter {
    value: int

    inc() { self.value += 1 }
}
```

## SDL3 Integration

```
init(0)
win := createWindow("Game", 800, 600, 0)
ren := createRenderer(win)

running := true
for running {
    evt := pollEvent()
    for evt != 0 {
        if evt == 256 { running = false }
        evt = pollEvent()
    }

    setDrawColor(ren, 0, 0, 0, 255)
    clear(ren)
    present(ren)
    delay(16)
}

quit()
```

## SharoUI

Native UI component library in `std/sharoui/`:

```
import "std/sharoui/theme.sharo"
import "std/sharoui/button.sharo"

// In game loop:
if drawButton(ren, font, "Click", 50, 50, 100, 32, mx, my, clicked, BTN_PRIMARY) {
    print("clicked")
}
```

Components: button, input, textarea, checkbox, slider, dropdown, tabs, modal, progress, list, tooltip, icons.

## Requirements

- GCC/Clang (C99)
- SDL3, SDL3_ttf, SDL3_image
- pkg-config

```bash
# Fedora
sudo dnf install SDL3-devel SDL3_ttf-devel SDL3_image-devel

# Build from source if packages unavailable
```

## License

MIT
