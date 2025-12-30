# Sharo SDL3 API Reference

This document lists all SDL3 functions to be implemented as Sharo native functions.

## Naming Convention

SDL3 functions are exposed without the `SDL_` prefix:
- `SDL_Init` -> `init`
- `SDL_CreateWindow` -> `createWindow`
- `SDL_RenderClear` -> `clear`

## Implementation Status

- [x] Implemented
- [ ] Not yet implemented

---

## Initialization & Shutdown

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_Init | init | [x] | `init(flags int) -> bool` |
| SDL_Quit | quit | [x] | `quit()` |
| SDL_WasInit | wasInit | [ ] | `wasInit(flags int) -> int` |
| SDL_InitSubSystem | initSubSystem | [ ] | `initSubSystem(flags int) -> bool` |
| SDL_QuitSubSystem | quitSubSystem | [ ] | `quitSubSystem(flags int)` |

### Init Flags
```
INIT_AUDIO    := 0x00000010
INIT_VIDEO    := 0x00000020
INIT_JOYSTICK := 0x00000200
INIT_HAPTIC   := 0x00001000
INIT_GAMEPAD  := 0x00002000
INIT_EVENTS   := 0x00004000
INIT_SENSOR   := 0x00008000
INIT_CAMERA   := 0x00010000
```

---

## Window Management

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_CreateWindow | createWindow | [x] | `createWindow(title str, w int, h int, flags int) -> ptr` |
| SDL_DestroyWindow | destroyWindow | [x] | `destroyWindow(window ptr)` |
| SDL_GetWindowSize | getWindowSize | [ ] | `getWindowSize(window ptr) -> (int, int)` |
| SDL_SetWindowSize | setWindowSize | [ ] | `setWindowSize(window ptr, w int, h int)` |
| SDL_GetWindowPosition | getWindowPosition | [ ] | `getWindowPosition(window ptr) -> (int, int)` |
| SDL_SetWindowPosition | setWindowPosition | [ ] | `setWindowPosition(window ptr, x int, y int)` |
| SDL_SetWindowTitle | setWindowTitle | [ ] | `setWindowTitle(window ptr, title str)` |
| SDL_GetWindowTitle | getWindowTitle | [ ] | `getWindowTitle(window ptr) -> str` |
| SDL_ShowWindow | showWindow | [ ] | `showWindow(window ptr)` |
| SDL_HideWindow | hideWindow | [ ] | `hideWindow(window ptr)` |
| SDL_RaiseWindow | raiseWindow | [ ] | `raiseWindow(window ptr)` |
| SDL_MaximizeWindow | maximizeWindow | [ ] | `maximizeWindow(window ptr)` |
| SDL_MinimizeWindow | minimizeWindow | [ ] | `minimizeWindow(window ptr)` |
| SDL_RestoreWindow | restoreWindow | [ ] | `restoreWindow(window ptr)` |
| SDL_SetWindowFullscreen | setWindowFullscreen | [ ] | `setWindowFullscreen(window ptr, fullscreen bool)` |
| SDL_GetWindowFlags | getWindowFlags | [ ] | `getWindowFlags(window ptr) -> int` |

### Window Flags
```
WINDOW_FULLSCREEN         := 0x00000001
WINDOW_OPENGL             := 0x00000002
WINDOW_OCCLUDED           := 0x00000004
WINDOW_HIDDEN             := 0x00000008
WINDOW_BORDERLESS         := 0x00000010
WINDOW_RESIZABLE          := 0x00000020
WINDOW_MINIMIZED          := 0x00000040
WINDOW_MAXIMIZED          := 0x00000080
WINDOW_MOUSE_GRABBED      := 0x00000100
WINDOW_INPUT_FOCUS        := 0x00000200
WINDOW_MOUSE_FOCUS        := 0x00000400
WINDOW_HIGH_PIXEL_DENSITY := 0x00002000
WINDOW_MOUSE_CAPTURE      := 0x00004000
WINDOW_ALWAYS_ON_TOP      := 0x00008000
WINDOW_VULKAN             := 0x10000000
WINDOW_METAL              := 0x20000000
```

---

## 2D Rendering

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_CreateRenderer | createRenderer | [x] | `createRenderer(window ptr) -> ptr` |
| SDL_DestroyRenderer | destroyRenderer | [x] | `destroyRenderer(renderer ptr)` |
| SDL_RenderClear | clear | [x] | `clear(renderer ptr) -> bool` |
| SDL_RenderPresent | present | [x] | `present(renderer ptr) -> bool` |
| SDL_SetRenderDrawColor | setDrawColor | [x] | `setDrawColor(renderer ptr, r int, g int, b int, a int) -> bool` |
| SDL_GetRenderDrawColor | getDrawColor | [ ] | `getDrawColor(renderer ptr) -> (int, int, int, int)` |
| SDL_RenderPoint | drawPoint | [ ] | `drawPoint(renderer ptr, x float, y float) -> bool` |
| SDL_RenderLine | drawLine | [ ] | `drawLine(renderer ptr, x1 float, y1 float, x2 float, y2 float) -> bool` |
| SDL_RenderRect | drawRect | [x] | `drawRect(renderer ptr, x float, y float, w float, h float) -> bool` |
| SDL_RenderFillRect | fillRect | [x] | `fillRect(renderer ptr, x float, y float, w float, h float) -> bool` |
| SDL_SetRenderViewport | setViewport | [ ] | `setViewport(renderer ptr, x int, y int, w int, h int)` |
| SDL_SetRenderClipRect | setClipRect | [ ] | `setClipRect(renderer ptr, x int, y int, w int, h int)` |
| SDL_SetRenderVSync | setVSync | [ ] | `setVSync(renderer ptr, vsync int) -> bool` |
| SDL_SetRenderDrawBlendMode | setBlendMode | [ ] | `setBlendMode(renderer ptr, mode int) -> bool` |

### Blend Modes
```
BLENDMODE_NONE  := 0x00000000
BLENDMODE_BLEND := 0x00000001
BLENDMODE_ADD   := 0x00000002
BLENDMODE_MOD   := 0x00000004
BLENDMODE_MUL   := 0x00000008
```

---

## Textures

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_CreateTexture | createTexture | [ ] | `createTexture(renderer ptr, format int, access int, w int, h int) -> ptr` |
| SDL_CreateTextureFromSurface | createTextureFromSurface | [ ] | `createTextureFromSurface(renderer ptr, surface ptr) -> ptr` |
| SDL_DestroyTexture | destroyTexture | [ ] | `destroyTexture(texture ptr)` |
| SDL_RenderTexture | renderTexture | [ ] | `renderTexture(renderer ptr, texture ptr, srcX float, srcY float, srcW float, srcH float, dstX float, dstY float, dstW float, dstH float) -> bool` |
| SDL_RenderTextureRotated | renderTextureRotated | [ ] | `renderTextureRotated(renderer ptr, texture ptr, ...) -> bool` |
| SDL_UpdateTexture | updateTexture | [ ] | `updateTexture(texture ptr, ...) -> bool` |
| SDL_SetTextureColorMod | setTextureColorMod | [ ] | `setTextureColorMod(texture ptr, r int, g int, b int) -> bool` |
| SDL_SetTextureAlphaMod | setTextureAlphaMod | [ ] | `setTextureAlphaMod(texture ptr, a int) -> bool` |
| SDL_SetTextureBlendMode | setTextureBlendMode | [ ] | `setTextureBlendMode(texture ptr, mode int) -> bool` |

---

## Surfaces & Image Loading

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_LoadBMP | loadBMP | [ ] | `loadBMP(path str) -> ptr` |
| SDL_SaveBMP | saveBMP | [ ] | `saveBMP(surface ptr, path str) -> bool` |
| SDL_CreateSurface | createSurface | [ ] | `createSurface(w int, h int, format int) -> ptr` |
| SDL_DestroySurface | destroySurface | [ ] | `destroySurface(surface ptr)` |
| SDL_FillSurfaceRect | fillSurfaceRect | [ ] | `fillSurfaceRect(surface ptr, x int, y int, w int, h int, color int) -> bool` |
| SDL_BlitSurface | blitSurface | [ ] | `blitSurface(src ptr, srcRect ptr, dst ptr, dstRect ptr) -> bool` |

---

## Events

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_PollEvent | pollEvent | [x] | `pollEvent() -> int` (returns event type, 0 if none) |
| SDL_WaitEvent | waitEvent | [ ] | `waitEvent() -> int` |
| SDL_WaitEventTimeout | waitEventTimeout | [ ] | `waitEventTimeout(timeout int) -> int` |
| SDL_PushEvent | pushEvent | [ ] | `pushEvent(type int) -> bool` |
| SDL_HasEvent | hasEvent | [ ] | `hasEvent(type int) -> bool` |
| SDL_FlushEvent | flushEvent | [ ] | `flushEvent(type int)` |
| SDL_FlushEvents | flushEvents | [ ] | `flushEvents(minType int, maxType int)` |

### Event Accessors (Sharo-specific)
| Function | Status | Signature |
|----------|--------|-----------|
| eventKey | [x] | `eventKey() -> int` (scancode from last key event) |
| eventMouseX | [ ] | `eventMouseX() -> int` |
| eventMouseY | [ ] | `eventMouseY() -> int` |
| eventMouseButton | [ ] | `eventMouseButton() -> int` |
| eventWindowID | [ ] | `eventWindowID() -> int` |

### Event Types
```
EVENT_QUIT              := 0x100  // 256
EVENT_TERMINATING       := 0x101
EVENT_LOW_MEMORY        := 0x102
EVENT_WILL_ENTER_BG     := 0x103
EVENT_DID_ENTER_BG      := 0x104
EVENT_WILL_ENTER_FG     := 0x105
EVENT_DID_ENTER_FG      := 0x106

EVENT_KEY_DOWN          := 0x300  // 768
EVENT_KEY_UP            := 0x301
EVENT_TEXT_EDITING      := 0x302
EVENT_TEXT_INPUT        := 0x303

EVENT_MOUSE_MOTION      := 0x400  // 1024
EVENT_MOUSE_BUTTON_DOWN := 0x401
EVENT_MOUSE_BUTTON_UP   := 0x402
EVENT_MOUSE_WHEEL       := 0x403

EVENT_JOYSTICK_AXIS     := 0x600
EVENT_JOYSTICK_BUTTON_DOWN := 0x603
EVENT_JOYSTICK_BUTTON_UP   := 0x604

EVENT_GAMEPAD_AXIS      := 0x650
EVENT_GAMEPAD_BUTTON_DOWN := 0x651
EVENT_GAMEPAD_BUTTON_UP   := 0x652

EVENT_WINDOW_SHOWN      := 0x202
EVENT_WINDOW_HIDDEN     := 0x203
EVENT_WINDOW_MOVED      := 0x205
EVENT_WINDOW_RESIZED    := 0x206
EVENT_WINDOW_CLOSE_REQUESTED := 0x215
```

---

## Keyboard

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_GetKeyboardState | getKeyboardState | [ ] | `getKeyboardState() -> ptr` |
| SDL_GetKeyFromScancode | keyFromScancode | [ ] | `keyFromScancode(scancode int) -> int` |
| SDL_GetScancodeFromKey | scancodeFromKey | [ ] | `scancodeFromKey(key int) -> int` |
| SDL_GetKeyName | getKeyName | [ ] | `getKeyName(key int) -> str` |
| SDL_GetScancodeName | getScancodeName | [ ] | `getScancodeName(scancode int) -> str` |
| SDL_GetModState | getModState | [ ] | `getModState() -> int` |
| SDL_StartTextInput | startTextInput | [ ] | `startTextInput()` |
| SDL_StopTextInput | stopTextInput | [ ] | `stopTextInput()` |

### Common Scancodes
```
KEY_A := 4   KEY_B := 5   KEY_C := 6   ... KEY_Z := 29
KEY_1 := 30  KEY_2 := 31  ... KEY_0 := 39
KEY_RETURN := 40
KEY_ESCAPE := 41
KEY_BACKSPACE := 42
KEY_TAB := 43
KEY_SPACE := 44
KEY_RIGHT := 79
KEY_LEFT := 80
KEY_DOWN := 81
KEY_UP := 82
KEY_LCTRL := 224
KEY_LSHIFT := 225
KEY_LALT := 226
```

---

## Mouse

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_GetMouseState | getMouseState | [ ] | `getMouseState() -> (int, int, int)` (x, y, buttons) |
| SDL_GetGlobalMouseState | getGlobalMouseState | [ ] | `getGlobalMouseState() -> (int, int, int)` |
| SDL_GetRelativeMouseState | getRelativeMouseState | [ ] | `getRelativeMouseState() -> (int, int)` |
| SDL_WarpMouseInWindow | warpMouse | [ ] | `warpMouse(window ptr, x int, y int)` |
| SDL_SetWindowRelativeMouseMode | setRelativeMouseMode | [ ] | `setRelativeMouseMode(window ptr, enabled bool) -> bool` |
| SDL_ShowCursor | showCursor | [ ] | `showCursor() -> bool` |
| SDL_HideCursor | hideCursor | [ ] | `hideCursor() -> bool` |
| SDL_CreateCursor | createCursor | [ ] | `createCursor(...) -> ptr` |
| SDL_CreateSystemCursor | createSystemCursor | [ ] | `createSystemCursor(id int) -> ptr` |
| SDL_SetCursor | setCursor | [ ] | `setCursor(cursor ptr)` |
| SDL_DestroyCursor | destroyCursor | [ ] | `destroyCursor(cursor ptr)` |

### Mouse Buttons
```
BUTTON_LEFT   := 1
BUTTON_MIDDLE := 2
BUTTON_RIGHT  := 3
BUTTON_X1     := 4
BUTTON_X2     := 5
```

---

## Joystick & Gamepad

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_GetJoysticks | getJoysticks | [ ] | `getJoysticks() -> ptr` |
| SDL_OpenJoystick | openJoystick | [ ] | `openJoystick(id int) -> ptr` |
| SDL_CloseJoystick | closeJoystick | [ ] | `closeJoystick(joystick ptr)` |
| SDL_GetJoystickAxis | getJoystickAxis | [ ] | `getJoystickAxis(joystick ptr, axis int) -> int` |
| SDL_GetJoystickButton | getJoystickButton | [ ] | `getJoystickButton(joystick ptr, button int) -> bool` |
| SDL_OpenGamepad | openGamepad | [ ] | `openGamepad(id int) -> ptr` |
| SDL_CloseGamepad | closeGamepad | [ ] | `closeGamepad(gamepad ptr)` |
| SDL_GetGamepadAxis | getGamepadAxis | [ ] | `getGamepadAxis(gamepad ptr, axis int) -> int` |
| SDL_GetGamepadButton | getGamepadButton | [ ] | `getGamepadButton(gamepad ptr, button int) -> bool` |

---

## Audio

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_OpenAudioDevice | openAudioDevice | [ ] | `openAudioDevice(deviceId int, spec ptr) -> ptr` |
| SDL_CloseAudioDevice | closeAudioDevice | [ ] | `closeAudioDevice(device ptr)` |
| SDL_PauseAudioDevice | pauseAudio | [ ] | `pauseAudio(device ptr)` |
| SDL_ResumeAudioDevice | resumeAudio | [ ] | `resumeAudio(device ptr)` |
| SDL_CreateAudioStream | createAudioStream | [ ] | `createAudioStream(srcSpec ptr, dstSpec ptr) -> ptr` |
| SDL_DestroyAudioStream | destroyAudioStream | [ ] | `destroyAudioStream(stream ptr)` |
| SDL_PutAudioStreamData | putAudioData | [ ] | `putAudioData(stream ptr, data ptr, len int) -> bool` |
| SDL_LoadWAV | loadWAV | [ ] | `loadWAV(path str) -> (ptr, ptr, int)` |

---

## Time

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_GetTicks | getTicks | [x] | `getTicks() -> int` |
| SDL_Delay | delay | [x] | `delay(ms int)` |
| SDL_GetPerformanceCounter | getPerformanceCounter | [ ] | `getPerformanceCounter() -> int` |
| SDL_GetPerformanceFrequency | getPerformanceFrequency | [ ] | `getPerformanceFrequency() -> int` |
| SDL_AddTimer | addTimer | [ ] | `addTimer(interval int, callback fn) -> int` |
| SDL_RemoveTimer | removeTimer | [ ] | `removeTimer(id int) -> bool` |

---

## File I/O

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_IOFromFile | openFile | [ ] | `openFile(path str, mode str) -> ptr` |
| SDL_CloseIO | closeFile | [ ] | `closeFile(io ptr) -> bool` |
| SDL_ReadIO | readFile | [ ] | `readFile(io ptr, size int) -> ptr` |
| SDL_WriteIO | writeFile | [ ] | `writeFile(io ptr, data ptr, size int) -> int` |
| SDL_GetBasePath | getBasePath | [ ] | `getBasePath() -> str` |
| SDL_GetPrefPath | getPrefPath | [ ] | `getPrefPath(org str, app str) -> str` |

---

## Clipboard

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_SetClipboardText | setClipboardText | [ ] | `setClipboardText(text str) -> bool` |
| SDL_GetClipboardText | getClipboardText | [ ] | `getClipboardText() -> str` |
| SDL_HasClipboardText | hasClipboardText | [ ] | `hasClipboardText() -> bool` |

---

## Error Handling

| SDL3 Function | Sharo Name | Status | Signature |
|---------------|------------|--------|-----------|
| SDL_GetError | getError | [ ] | `getError() -> str` |
| SDL_ClearError | clearError | [ ] | `clearError()` |

---

## Implementation Notes

### Structs as Multiple Returns
Since Sharo doesn't have structs yet, functions that return SDL structs will either:
1. Return multiple values (when supported)
2. Use global state with accessor functions (like `eventKey()`)
3. Return a ptr that can be queried with accessor functions

### Callbacks
Functions requiring callbacks (like `SDL_AddTimer`) need special handling:
- Option 1: Accept a Sharo function and wrap it
- Option 2: Provide event-based alternatives

### Memory Management
- Sharo's GC doesn't track SDL resources
- Users must explicitly call `destroy*` functions
- Consider adding reference counting or weak references later

---

## Priority Implementation Order

### Phase 1: Core Game Loop (DONE)
- [x] init, quit
- [x] createWindow, destroyWindow
- [x] createRenderer, destroyRenderer
- [x] clear, present, setDrawColor
- [x] fillRect, drawRect
- [x] pollEvent, eventKey
- [x] delay, getTicks

### Phase 2: Basic Graphics
- [ ] drawPoint, drawLine
- [ ] Texture loading and rendering
- [ ] loadBMP, createTextureFromSurface

### Phase 3: Input
- [ ] getKeyboardState
- [ ] getMouseState, mouse position accessors
- [ ] Gamepad support

### Phase 4: Audio
- [ ] loadWAV
- [ ] Audio device and stream management
- [ ] Basic sound playback

### Phase 5: Advanced
- [ ] Fullscreen modes
- [ ] Multiple windows
- [ ] Clipboard
- [ ] File I/O
