#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// TCP sockets (POSIX)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Directory operations (POSIX)
#include <dirent.h>
#include <sys/stat.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>

#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

VM vm;

// Forward declaration
static ObjString* valueToString(Value value);

// Read file for module loading
static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static void resetStack(void) {
    vm.stackTop = vm.stack;
    vm.frameCount = 0;
    vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk->code - 1;
        fprintf(stderr, "[line %d] in ", function->chunk->lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    resetStack();
}

// Native function: clock()
static Value clockNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return FLOAT_VAL((double)clock() / CLOCKS_PER_SEC);
}

// error(message) - Print error and exit
static Value errorNative(int argCount, Value* args) {
    (void)argCount;
    fprintf(stderr, "Error: ");
    if (IS_STRING(args[0])) {
        fprintf(stderr, "%s", AS_CSTRING(args[0]));
    } else {
        printValue(args[0]);
    }
    fprintf(stderr, "\n");

    // Print stack trace
    for (int i = vm.frameCount - 1; i >= 0; i--) {
        CallFrame* frame = &vm.frames[i];
        ObjFunction* function = frame->closure->function;
        size_t instruction = frame->ip - function->chunk->code - 1;
        fprintf(stderr, "  at [line %d] in ", function->chunk->lines[instruction]);
        if (function->name == NULL) {
            fprintf(stderr, "script\n");
        } else {
            fprintf(stderr, "%s()\n", function->name->chars);
        }
    }

    exit(1);
    return NIL_VAL;
}

// assert(condition, message) - Assert condition is true
static Value assertNative(int argCount, Value* args) {
    bool condition = false;
    if (IS_BOOL(args[0])) {
        condition = AS_BOOL(args[0]);
    } else if (IS_NIL(args[0])) {
        condition = false;
    } else {
        condition = true; // Truthy
    }

    if (!condition) {
        fprintf(stderr, "Assertion failed");
        if (argCount > 1 && IS_STRING(args[1])) {
            fprintf(stderr, ": %s", AS_CSTRING(args[1]));
        }
        fprintf(stderr, "\n");

        // Print stack trace
        for (int i = vm.frameCount - 1; i >= 0; i--) {
            CallFrame* frame = &vm.frames[i];
            ObjFunction* function = frame->closure->function;
            size_t instruction = frame->ip - function->chunk->code - 1;
            fprintf(stderr, "  at [line %d] in ", function->chunk->lines[instruction]);
            if (function->name == NULL) {
                fprintf(stderr, "script\n");
            } else {
                fprintf(stderr, "%s()\n", function->name->chars);
            }
        }

        exit(1);
    }
    return NIL_VAL;
}

// type(value) -> str - Get type name of value
static Value typeNative(int argCount, Value* args) {
    (void)argCount;
    const char* name;

    if (IS_NIL(args[0])) name = "nil";
    else if (IS_BOOL(args[0])) name = "bool";
    else if (IS_INT(args[0])) name = "int";
    else if (IS_FLOAT(args[0])) name = "float";
    else if (IS_PTR(args[0])) name = "ptr";
    else if (IS_STRING(args[0])) name = "str";
    else if (IS_ARRAY(args[0])) name = "array";
    else if (IS_STRUCT(args[0])) name = "struct";
    else if (IS_FUNCTION(args[0]) || IS_CLOSURE(args[0])) name = "function";
    else name = "unknown";

    return OBJ_VAL(copyString(name, (int)strlen(name)));
}

// ============ SDL3 Native Functions ============

// Global event storage for pollEvent
static SDL_Event currentEvent;

// init(flags) -> bool
static Value initNative(int argCount, Value* args) {
    (void)argCount;
    uint32_t flags = (uint32_t)AS_INT(args[0]);
    return BOOL_VAL(SDL_Init(flags));
}

// quit()
static Value quitNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    SDL_Quit();
    return NIL_VAL;
}

// createWindow(title, w, h, flags) -> ptr
static Value createWindowNative(int argCount, Value* args) {
    (void)argCount;
    const char* title = AS_CSTRING(args[0]);
    int w = (int)AS_INT(args[1]);
    int h = (int)AS_INT(args[2]);
    uint32_t flags = (uint32_t)AS_INT(args[3]);
    SDL_Window* window = SDL_CreateWindow(title, w, h, flags);
    return PTR_VAL(window);
}

// destroyWindow(window)
static Value destroyWindowNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Window* window = (SDL_Window*)AS_PTR(args[0]);
    SDL_DestroyWindow(window);
    return NIL_VAL;
}

// createRenderer(window) -> ptr
static Value createRendererNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Window* window = (SDL_Window*)AS_PTR(args[0]);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    return PTR_VAL(renderer);
}

// setLogicalSize(renderer, width, height) - enable auto-scaling
static Value setLogicalSizeNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    int w = (int)AS_INT(args[1]);
    int h = (int)AS_INT(args[2]);
    return BOOL_VAL(SDL_SetRenderLogicalPresentation(renderer, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX));
}

// destroyRenderer(renderer)
static Value destroyRendererNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    SDL_DestroyRenderer(renderer);
    return NIL_VAL;
}

// clear(renderer) -> bool
static Value clearNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    return BOOL_VAL(SDL_RenderClear(renderer));
}

// present(renderer) -> bool
static Value presentNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    return BOOL_VAL(SDL_RenderPresent(renderer));
}

// setDrawColor(renderer, r, g, b, a) -> bool
static Value setDrawColorNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    uint8_t r = (uint8_t)AS_INT(args[1]);
    uint8_t g = (uint8_t)AS_INT(args[2]);
    uint8_t b = (uint8_t)AS_INT(args[3]);
    uint8_t a = (uint8_t)AS_INT(args[4]);
    return BOOL_VAL(SDL_SetRenderDrawColor(renderer, r, g, b, a));
}

// setBlendMode(renderer, mode) - 0=none, 1=blend, 2=add, 3=mod
static Value setBlendModeNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    int mode = (int)AS_INT(args[1]);
    SDL_BlendMode blendMode = SDL_BLENDMODE_NONE;
    if (mode == 1) blendMode = SDL_BLENDMODE_BLEND;
    else if (mode == 2) blendMode = SDL_BLENDMODE_ADD;
    else if (mode == 3) blendMode = SDL_BLENDMODE_MOD;
    return BOOL_VAL(SDL_SetRenderDrawBlendMode(renderer, blendMode));
}

// fillRect(renderer, x, y, w, h) -> bool
static Value fillRectNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    SDL_FRect rect;
    rect.x = (float)AS_NUMBER(args[1]);
    rect.y = (float)AS_NUMBER(args[2]);
    rect.w = (float)AS_NUMBER(args[3]);
    rect.h = (float)AS_NUMBER(args[4]);
    return BOOL_VAL(SDL_RenderFillRect(renderer, &rect));
}

// drawRect(renderer, x, y, w, h) -> bool
static Value drawRectNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    SDL_FRect rect;
    rect.x = (float)AS_NUMBER(args[1]);
    rect.y = (float)AS_NUMBER(args[2]);
    rect.w = (float)AS_NUMBER(args[3]);
    rect.h = (float)AS_NUMBER(args[4]);
    return BOOL_VAL(SDL_RenderRect(renderer, &rect));
}

// drawLine(renderer, x1, y1, x2, y2)
static Value drawLineNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    float x1 = (float)AS_NUMBER(args[1]);
    float y1 = (float)AS_NUMBER(args[2]);
    float x2 = (float)AS_NUMBER(args[3]);
    float y2 = (float)AS_NUMBER(args[4]);
    return BOOL_VAL(SDL_RenderLine(renderer, x1, y1, x2, y2));
}

// pollEvent() -> int (event type, 0 if none)
static Value pollEventNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    if (SDL_PollEvent(&currentEvent)) {
        return INT_VAL((int64_t)currentEvent.type);
    }
    return INT_VAL(0);
}

// eventKey() -> int (scancode of last key event)
static Value eventKeyNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    if (currentEvent.type == SDL_EVENT_KEY_DOWN || currentEvent.type == SDL_EVENT_KEY_UP) {
        return INT_VAL((int64_t)currentEvent.key.scancode);
    }
    return INT_VAL(0);
}

// isKeyDown(scancode) -> bool (check if key is currently held)
static Value isKeyDownNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Scancode scancode = (SDL_Scancode)AS_INT(args[0]);
    const bool* keyStates = SDL_GetKeyboardState(NULL);
    return BOOL_VAL(keyStates[scancode]);
}

// startTextInput(window) - enable text input for a window
static Value startTextInputNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Window* window = (SDL_Window*)AS_PTR(args[0]);
    return BOOL_VAL(SDL_StartTextInput(window));
}

// stopTextInput(window) - disable text input
static Value stopTextInputNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Window* window = (SDL_Window*)AS_PTR(args[0]);
    return BOOL_VAL(SDL_StopTextInput(window));
}

// getTextInput() -> str (get text from TEXT_INPUT event)
static Value getTextInputNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    if (currentEvent.type == SDL_EVENT_TEXT_INPUT) {
        return OBJ_VAL(copyString(currentEvent.text.text, strlen(currentEvent.text.text)));
    }
    return OBJ_VAL(copyString("", 0));
}

// eventWindowW() -> int (window width from resize event)
static Value eventWindowWNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return INT_VAL((int64_t)currentEvent.window.data1);
}

// eventWindowH() -> int (window height from resize event)
static Value eventWindowHNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return INT_VAL((int64_t)currentEvent.window.data2);
}

// getMouseX() -> int
static Value getMouseXNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    float x, y;
    SDL_GetMouseState(&x, &y);
    return INT_VAL((int64_t)x);
}

// getMouseY() -> int
static Value getMouseYNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    float x, y;
    SDL_GetMouseState(&x, &y);
    return INT_VAL((int64_t)y);
}

// getLogicalMouseX(renderer) -> int (mouse X in logical coordinates)
static Value getLogicalMouseXNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    float wx, wy;
    SDL_GetMouseState(&wx, &wy);
    float lx, ly;
    SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &lx, &ly);
    return INT_VAL((int64_t)lx);
}

// getLogicalMouseY(renderer) -> int (mouse Y in logical coordinates)
static Value getLogicalMouseYNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    float wx, wy;
    SDL_GetMouseState(&wx, &wy);
    float lx, ly;
    SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &lx, &ly);
    return INT_VAL((int64_t)ly);
}

// getMouseButton() -> int (bitmask: 1=left, 2=middle, 4=right)
static Value getMouseButtonNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    float x, y;
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
    return INT_VAL((int64_t)buttons);
}

// getMouseWheelY() -> int (scroll direction: positive=up, negative=down)
static Value getMouseWheelYNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    if (currentEvent.type == SDL_EVENT_MOUSE_WHEEL) {
        return INT_VAL((int64_t)currentEvent.wheel.y);
    }
    return INT_VAL(0);
}

// delay(ms)
static Value delayNative(int argCount, Value* args) {
    (void)argCount;
    uint32_t ms = (uint32_t)AS_INT(args[0]);
    SDL_Delay(ms);
    return NIL_VAL;
}

// getTicks() -> int
static Value getTicksNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return INT_VAL((int64_t)SDL_GetTicks());
}

// loadTexture(renderer, path) -> ptr (loads BMP image)
static Value loadTextureNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    const char* path = AS_CSTRING(args[1]);

    SDL_Surface* surface = IMG_Load(path);
    if (surface == NULL) {
        fprintf(stderr, "Failed to load image: %s\n", SDL_GetError());
        return PTR_VAL(NULL);
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (texture == NULL) {
        fprintf(stderr, "Failed to create texture: %s\n", SDL_GetError());
        return PTR_VAL(NULL);
    }

    // Enable alpha blending for transparency
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    return PTR_VAL(texture);
}

// destroyTexture(texture)
static Value destroyTextureNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Texture* texture = (SDL_Texture*)AS_PTR(args[0]);
    if (texture != NULL) {
        SDL_DestroyTexture(texture);
    }
    return NIL_VAL;
}

// drawTexture(renderer, texture, x, y, w, h) -> bool
static Value drawTextureNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    SDL_Texture* texture = (SDL_Texture*)AS_PTR(args[1]);

    SDL_FRect dest;
    dest.x = (float)AS_NUMBER(args[2]);
    dest.y = (float)AS_NUMBER(args[3]);
    dest.w = (float)AS_NUMBER(args[4]);
    dest.h = (float)AS_NUMBER(args[5]);

    return BOOL_VAL(SDL_RenderTexture(renderer, texture, NULL, &dest));
}

// getTextureSize(texture) -> array [width, height]
static Value getTextureSizeNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Texture* texture = (SDL_Texture*)AS_PTR(args[0]);

    float w, h;
    SDL_GetTextureSize(texture, &w, &h);

    ObjArray* arr = newArray();
    writeArray(arr, INT_VAL((int64_t)w));
    writeArray(arr, INT_VAL((int64_t)h));
    return OBJ_VAL(arr);
}

// random(max) -> int (0 to max-1)
static Value randomNative(int argCount, Value* args) {
    (void)argCount;
    int64_t max = AS_INT(args[0]);
    if (max <= 0) return INT_VAL(0);
    return INT_VAL(rand() % max);
}

// randomFloat() -> float (0.0 to 1.0)
static Value randomFloatNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return FLOAT_VAL((double)rand() / RAND_MAX);
}

// sqrt(n) -> float
static Value sqrtNative(int argCount, Value* args) {
    (void)argCount;
    double n = AS_NUMBER(args[0]);
    return FLOAT_VAL(sqrt(n));
}

// floor(n) -> int (truncate towards negative infinity)
static Value floorNative(int argCount, Value* args) {
    (void)argCount;
    double n = AS_NUMBER(args[0]);
    return INT_VAL((int)floor(n));
}

// int(n) -> int (truncate towards zero)
static Value intNative(int argCount, Value* args) {
    (void)argCount;
    double n = AS_NUMBER(args[0]);
    return INT_VAL((int)n);
}

// sin(x) -> float
static Value sinNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(sin(AS_NUMBER(args[0])));
}

// cos(x) -> float
static Value cosNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(cos(AS_NUMBER(args[0])));
}

// tan(x) -> float
static Value tanNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(tan(AS_NUMBER(args[0])));
}

// asin(x) -> float
static Value asinNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(asin(AS_NUMBER(args[0])));
}

// acos(x) -> float
static Value acosNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(acos(AS_NUMBER(args[0])));
}

// atan(x) -> float
static Value atanNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(atan(AS_NUMBER(args[0])));
}

// atan2(y, x) -> float
static Value atan2Native(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(atan2(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

// abs(x) -> float
static Value absNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(fabs(AS_NUMBER(args[0])));
}

// pow(base, exp) -> float
static Value powNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(pow(AS_NUMBER(args[0]), AS_NUMBER(args[1])));
}

// ceil(x) -> int
static Value ceilNative(int argCount, Value* args) {
    (void)argCount;
    return INT_VAL((int64_t)ceil(AS_NUMBER(args[0])));
}

// round(x) -> int
static Value roundNative(int argCount, Value* args) {
    (void)argCount;
    return INT_VAL((int64_t)round(AS_NUMBER(args[0])));
}

// log(x) -> float (natural log)
static Value logNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(log(AS_NUMBER(args[0])));
}

// exp(x) -> float
static Value expNative(int argCount, Value* args) {
    (void)argCount;
    return FLOAT_VAL(exp(AS_NUMBER(args[0])));
}

// min(a, b) -> number
static Value minNative(int argCount, Value* args) {
    (void)argCount;
    double a = AS_NUMBER(args[0]);
    double b = AS_NUMBER(args[1]);
    return FLOAT_VAL(a < b ? a : b);
}

// max(a, b) -> number
static Value maxNative(int argCount, Value* args) {
    (void)argCount;
    double a = AS_NUMBER(args[0]);
    double b = AS_NUMBER(args[1]);
    return FLOAT_VAL(a > b ? a : b);
}

// ============ SDL3_ttf Native Functions ============

// initTTF() -> bool
static Value initTTFNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    return BOOL_VAL(TTF_Init());
}

// quitTTF()
static Value quitTTFNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;
    TTF_Quit();
    return NIL_VAL;
}

// loadFont(path, size) -> ptr
static Value loadFontNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);
    float size = (float)AS_NUMBER(args[1]);
    TTF_Font* font = TTF_OpenFont(path, size);
    if (font == NULL) {
        fprintf(stderr, "Failed to load font: %s\n", SDL_GetError());
        return PTR_VAL(NULL);
    }
    return PTR_VAL(font);
}

// destroyFont(font)
static Value destroyFontNative(int argCount, Value* args) {
    (void)argCount;
    TTF_Font* font = (TTF_Font*)AS_PTR(args[0]);
    if (font != NULL) {
        TTF_CloseFont(font);
    }
    return NIL_VAL;
}

// drawText(renderer, font, text, x, y, r, g, b) -> bool
static Value drawTextNative(int argCount, Value* args) {
    (void)argCount;
    SDL_Renderer* renderer = (SDL_Renderer*)AS_PTR(args[0]);
    TTF_Font* font = (TTF_Font*)AS_PTR(args[1]);
    const char* text = AS_CSTRING(args[2]);
    float x = (float)AS_NUMBER(args[3]);
    float y = (float)AS_NUMBER(args[4]);
    Uint8 r = (Uint8)AS_INT(args[5]);
    Uint8 g = (Uint8)AS_INT(args[6]);
    Uint8 b = (Uint8)AS_INT(args[7]);

    SDL_Color color = {r, g, b, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, 0, color);
    if (surface == NULL) {
        return BOOL_VAL(false);
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (texture == NULL) {
        return BOOL_VAL(false);
    }

    float w, h;
    SDL_GetTextureSize(texture, &w, &h);
    SDL_FRect dest = {x, y, w, h};
    SDL_RenderTexture(renderer, texture, NULL, &dest);
    SDL_DestroyTexture(texture);

    return BOOL_VAL(true);
}

// getTextWidth(font, text) -> int (width in pixels)
static Value getTextWidthNative(int argCount, Value* args) {
    (void)argCount;
    TTF_Font* font = (TTF_Font*)AS_PTR(args[0]);
    const char* text = AS_CSTRING(args[1]);

    int w = 0, h = 0;
    TTF_GetStringSize(font, text, 0, &w, &h);
    return INT_VAL((int64_t)w);
}

// ============ Array Native Functions ============

// len(arr) -> int (also works on strings)
static Value lenNative(int argCount, Value* args) {
    (void)argCount;
    if (IS_ARRAY(args[0])) {
        return INT_VAL(AS_ARRAY(args[0])->count);
    } else if (IS_STRING(args[0])) {
        return INT_VAL(AS_STRING(args[0])->length);
    }
    return INT_VAL(0);
}

// push(arr, value) -> int (new length)
static Value pushNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    writeArray(array, args[1]);
    return INT_VAL(array->count);
}

// pop(arr) -> value (last element)
static Value popNative(int argCount, Value* args) {
    (void)argCount;
    if (!IS_ARRAY(args[0])) {
        return NIL_VAL;
    }
    ObjArray* array = AS_ARRAY(args[0]);
    if (array->count == 0) {
        return NIL_VAL;
    }
    return array->elements[--array->count];
}

// ============ TCP Socket Native Functions ============

// tcpListen(port) -> socket ptr or nil on error
static Value tcpListenNative(int argCount, Value* args) {
    (void)argCount;
    int port = (int)AS_INT(args[0]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return NIL_VAL;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return NIL_VAL;
    }

    if (listen(sockfd, 10) < 0) {
        close(sockfd);
        return NIL_VAL;
    }

    return INT_VAL(sockfd);
}

// tcpAccept(socket) -> client socket or nil
static Value tcpAcceptNative(int argCount, Value* args) {
    (void)argCount;
    int sockfd = (int)AS_INT(args[0]);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
    if (clientfd < 0) {
        return NIL_VAL;
    }

    return INT_VAL(clientfd);
}

// tcpRecv(socket, maxLen) -> string or nil
static Value tcpRecvNative(int argCount, Value* args) {
    (void)argCount;
    int sockfd = (int)AS_INT(args[0]);
    int maxLen = (int)AS_INT(args[1]);

    char* buffer = malloc(maxLen + 1);
    if (!buffer) return NIL_VAL;

    ssize_t received = recv(sockfd, buffer, maxLen, 0);
    if (received <= 0) {
        free(buffer);
        return NIL_VAL;
    }

    buffer[received] = '\0';
    ObjString* str = copyString(buffer, (int)received);
    free(buffer);
    return OBJ_VAL(str);
}

// tcpSend(socket, data) -> bytes sent or -1
static Value tcpSendNative(int argCount, Value* args) {
    (void)argCount;
    int sockfd = (int)AS_INT(args[0]);
    ObjString* data = AS_STRING(args[1]);

    ssize_t sent = send(sockfd, data->chars, data->length, 0);
    return INT_VAL(sent);
}

// tcpClose(socket)
static Value tcpCloseNative(int argCount, Value* args) {
    (void)argCount;
    int sockfd = (int)AS_INT(args[0]);
    close(sockfd);
    return NIL_VAL;
}

// ============ Audio Native Functions ============

// Simple sound structure for WAV playback
typedef struct {
    SDL_AudioSpec spec;
    Uint8* buffer;
    Uint32 length;
} SoundData;

static SDL_AudioStream* audioStream = NULL;

// ============ Synthesizer for MIDI ============

#define MAX_VOICES 32
#define SAMPLE_RATE 44100

typedef struct {
    bool active;
    int note;           // MIDI note number
    float frequency;
    float phase;
    float velocity;     // 0.0-1.0
    float envelope;     // ADSR envelope value
    int stage;          // 0=attack, 1=decay, 2=sustain, 3=release
    bool releasing;
} Voice;

typedef struct {
    Voice voices[MAX_VOICES];
    float attack;       // seconds
    float decay;        // seconds
    float sustain;      // level 0-1
    float release;      // seconds
    float masterVolume;
    SDL_AudioStream* stream;
    bool initialized;
} Synth;

static Synth synth = {0};

static float midiNoteToFreq(int note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

static void synthCallback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
    (void)userdata;
    (void)total_amount;

    if (additional_amount <= 0) return;

    int samples = additional_amount / sizeof(int16_t);
    int16_t* buffer = malloc(additional_amount);
    if (!buffer) return;

    float dt = 1.0f / SAMPLE_RATE;

    for (int i = 0; i < samples; i++) {
        float sample = 0.0f;

        for (int v = 0; v < MAX_VOICES; v++) {
            Voice* voice = &synth.voices[v];
            if (!voice->active) continue;

            // Update envelope
            switch (voice->stage) {
                case 0: // Attack
                    voice->envelope += dt / synth.attack;
                    if (voice->envelope >= 1.0f) {
                        voice->envelope = 1.0f;
                        voice->stage = 1;
                    }
                    break;
                case 1: // Decay
                    voice->envelope -= dt / synth.decay * (1.0f - synth.sustain);
                    if (voice->envelope <= synth.sustain) {
                        voice->envelope = synth.sustain;
                        voice->stage = 2;
                    }
                    break;
                case 2: // Sustain
                    if (voice->releasing) voice->stage = 3;
                    break;
                case 3: // Release
                    voice->envelope -= dt / synth.release;
                    if (voice->envelope <= 0.0f) {
                        voice->envelope = 0.0f;
                        voice->active = false;
                    }
                    break;
            }

            // Generate waveform (sine + slight harmonics for richness)
            float wave = sinf(voice->phase * 2.0f * 3.14159265f);
            wave += 0.3f * sinf(voice->phase * 4.0f * 3.14159265f); // 2nd harmonic
            wave += 0.1f * sinf(voice->phase * 6.0f * 3.14159265f); // 3rd harmonic
            wave *= 0.5f; // Normalize

            sample += wave * voice->envelope * voice->velocity;

            // Advance phase
            voice->phase += voice->frequency * dt;
            if (voice->phase >= 1.0f) voice->phase -= 1.0f;
        }

        // Soft clip and convert to int16
        sample *= synth.masterVolume;
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        buffer[i] = (int16_t)(sample * 32000);
    }

    SDL_PutAudioStreamData(stream, buffer, additional_amount);
    free(buffer);
}

// initAudio() -> bool
static Value initAudioNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;

    // Create an audio stream that handles format conversion and output
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = 44100;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;

    audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                            &spec, NULL, NULL);
    if (!audioStream) {
        fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
        return BOOL_VAL(false);
    }

    SDL_ResumeAudioStreamDevice(audioStream);
    return BOOL_VAL(true);
}

// loadSound(path) -> ptr (SoundData*)
static Value loadSoundNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    SoundData* sound = malloc(sizeof(SoundData));
    if (!sound) return PTR_VAL(NULL);

    if (!SDL_LoadWAV(path, &sound->spec, &sound->buffer, &sound->length)) {
        fprintf(stderr, "Failed to load WAV: %s\n", SDL_GetError());
        free(sound);
        return PTR_VAL(NULL);
    }

    return PTR_VAL(sound);
}

// playSound(sound) -> bool
static Value playSoundNative(int argCount, Value* args) {
    (void)argCount;
    SoundData* sound = (SoundData*)AS_PTR(args[0]);
    if (!sound || !audioStream) return BOOL_VAL(false);

    // Get output format from our stream
    SDL_AudioSpec outSpec;
    SDL_zero(outSpec);
    outSpec.freq = 44100;
    outSpec.format = SDL_AUDIO_S16;
    outSpec.channels = 2;

    // Create a conversion stream if formats differ
    if (sound->spec.freq != outSpec.freq ||
        sound->spec.format != outSpec.format ||
        sound->spec.channels != outSpec.channels) {
        SDL_AudioStream* conv = SDL_CreateAudioStream(&sound->spec, &outSpec);
        if (conv) {
            SDL_PutAudioStreamData(conv, sound->buffer, sound->length);
            SDL_FlushAudioStream(conv);
            int available = SDL_GetAudioStreamAvailable(conv);
            if (available > 0) {
                Uint8* converted = malloc(available);
                if (converted) {
                    SDL_GetAudioStreamData(conv, converted, available);
                    SDL_PutAudioStreamData(audioStream, converted, available);
                    free(converted);
                }
            }
            SDL_DestroyAudioStream(conv);
        }
    } else {
        SDL_PutAudioStreamData(audioStream, sound->buffer, sound->length);
    }

    return BOOL_VAL(true);
}

// destroySound(sound)
static Value destroySoundNative(int argCount, Value* args) {
    (void)argCount;
    SoundData* sound = (SoundData*)AS_PTR(args[0]);
    if (sound) {
        if (sound->buffer) SDL_free(sound->buffer);
        free(sound);
    }
    return NIL_VAL;
}

// ============ Synthesizer Native Functions ============

// initSynth() -> bool
static Value initSynthNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;

    if (synth.initialized) return BOOL_VAL(true);

    // Initialize audio subsystem if not already done
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            fprintf(stderr, "Failed to init audio subsystem: %s\n", SDL_GetError());
            return BOOL_VAL(false);
        }
    }

    // Default ADSR
    synth.attack = 0.01f;
    synth.decay = 0.1f;
    synth.sustain = 0.7f;
    synth.release = 0.3f;
    synth.masterVolume = 0.5f;

    // Clear voices
    for (int i = 0; i < MAX_VOICES; i++) {
        synth.voices[i].active = false;
    }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = SAMPLE_RATE;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;

    synth.stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec, synthCallback, NULL);

    if (!synth.stream) {
        fprintf(stderr, "Failed to open synth audio: %s\n", SDL_GetError());
        return BOOL_VAL(false);
    }

    SDL_ResumeAudioStreamDevice(synth.stream);
    synth.initialized = true;
    return BOOL_VAL(true);
}

// noteOn(note, velocity) - note: 0-127 MIDI, velocity: 0-127
static Value noteOnNative(int argCount, Value* args) {
    (void)argCount;
    int note = (int)AS_INT(args[0]);
    int velocity = (int)AS_INT(args[1]);

    if (!synth.initialized) return NIL_VAL;

    // Find free voice or steal oldest
    int freeVoice = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!synth.voices[i].active) {
            freeVoice = i;
            break;
        }
    }
    if (freeVoice < 0) freeVoice = 0; // Steal first voice

    Voice* v = &synth.voices[freeVoice];
    v->active = true;
    v->note = note;
    v->frequency = midiNoteToFreq(note);
    v->phase = 0.0f;
    v->velocity = velocity / 127.0f;
    v->envelope = 0.0f;
    v->stage = 0;
    v->releasing = false;

    return INT_VAL(freeVoice);
}

// noteOff(note) - release note
static Value noteOffNative(int argCount, Value* args) {
    (void)argCount;
    int note = (int)AS_INT(args[0]);

    if (!synth.initialized) return NIL_VAL;

    for (int i = 0; i < MAX_VOICES; i++) {
        if (synth.voices[i].active && synth.voices[i].note == note) {
            synth.voices[i].releasing = true;
        }
    }
    return NIL_VAL;
}

// allNotesOff() - silence all
static Value allNotesOffNative(int argCount, Value* args) {
    (void)argCount;
    (void)args;

    for (int i = 0; i < MAX_VOICES; i++) {
        synth.voices[i].releasing = true;
    }
    return NIL_VAL;
}

// setSynthVolume(vol) - 0.0 to 1.0
static Value setSynthVolumeNative(int argCount, Value* args) {
    (void)argCount;
    synth.masterVolume = (float)AS_NUMBER(args[0]);
    return NIL_VAL;
}

// ============ MIDI File Parsing ============

typedef struct {
    uint8_t* data;
    size_t length;
    size_t pos;
} MidiBuffer;

typedef struct {
    int track;
    uint32_t tick;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} MidiEvent;

typedef struct {
    uint16_t format;
    uint16_t numTracks;
    uint16_t ticksPerBeat;
    MidiEvent* events;
    int eventCount;
    int eventCapacity;
    uint32_t tempo;  // microseconds per beat
} MidiFile;

static uint32_t readVarLen(MidiBuffer* buf) {
    uint32_t value = 0;
    uint8_t c;
    do {
        if (buf->pos >= buf->length) return 0;
        c = buf->data[buf->pos++];
        value = (value << 7) | (c & 0x7F);
    } while (c & 0x80);
    return value;
}

static uint16_t read16BE(MidiBuffer* buf) {
    if (buf->pos + 2 > buf->length) return 0;
    uint16_t v = (buf->data[buf->pos] << 8) | buf->data[buf->pos + 1];
    buf->pos += 2;
    return v;
}

static uint32_t read32BE(MidiBuffer* buf) {
    if (buf->pos + 4 > buf->length) return 0;
    uint32_t v = (buf->data[buf->pos] << 24) | (buf->data[buf->pos + 1] << 16) |
                 (buf->data[buf->pos + 2] << 8) | buf->data[buf->pos + 3];
    buf->pos += 4;
    return v;
}

static void addMidiEvent(MidiFile* midi, MidiEvent ev) {
    if (midi->eventCount >= midi->eventCapacity) {
        midi->eventCapacity = midi->eventCapacity ? midi->eventCapacity * 2 : 1024;
        midi->events = realloc(midi->events, midi->eventCapacity * sizeof(MidiEvent));
    }
    midi->events[midi->eventCount++] = ev;
}

static int compareMidiEvents(const void* a, const void* b) {
    const MidiEvent* ea = a;
    const MidiEvent* eb = b;
    if (ea->tick != eb->tick) return (int)ea->tick - (int)eb->tick;
    return ea->track - eb->track;
}

// loadMidi(path) -> ptr (MidiFile*)
static Value loadMidiNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open MIDI file: %s\n", path);
        return PTR_VAL(NULL);
    }

    fseek(f, 0, SEEK_END);
    size_t length = ftell(f);
    rewind(f);

    uint8_t* data = malloc(length);
    if (!data || fread(data, 1, length, f) != length) {
        fclose(f);
        free(data);
        return PTR_VAL(NULL);
    }
    fclose(f);

    MidiBuffer buf = {data, length, 0};

    // Check header "MThd"
    if (buf.length < 14 || memcmp(buf.data, "MThd", 4) != 0) {
        free(data);
        return PTR_VAL(NULL);
    }
    buf.pos = 4;

    uint32_t headerLen = read32BE(&buf);
    (void)headerLen;

    MidiFile* midi = calloc(1, sizeof(MidiFile));
    midi->format = read16BE(&buf);
    midi->numTracks = read16BE(&buf);
    midi->ticksPerBeat = read16BE(&buf);
    midi->tempo = 500000; // Default 120 BPM

    // Parse tracks
    for (int t = 0; t < midi->numTracks && buf.pos < buf.length; t++) {
        if (buf.pos + 8 > buf.length) break;
        if (memcmp(buf.data + buf.pos, "MTrk", 4) != 0) break;
        buf.pos += 4;

        uint32_t trackLen = read32BE(&buf);
        size_t trackEnd = buf.pos + trackLen;

        uint32_t tick = 0;
        uint8_t runningStatus = 0;

        while (buf.pos < trackEnd && buf.pos < buf.length) {
            uint32_t delta = readVarLen(&buf);
            tick += delta;

            if (buf.pos >= buf.length) break;
            uint8_t status = buf.data[buf.pos];

            if (status < 0x80) {
                status = runningStatus;
            } else {
                buf.pos++;
                if (status < 0xF0) runningStatus = status;
            }

            uint8_t type = status & 0xF0;

            if (type == 0x80 || type == 0x90) {
                // Note off/on
                if (buf.pos + 2 > buf.length) break;
                uint8_t note = buf.data[buf.pos++];
                uint8_t vel = buf.data[buf.pos++];

                MidiEvent ev = {t, tick, status, note, vel};
                addMidiEvent(midi, ev);
            } else if (type == 0xA0 || type == 0xB0 || type == 0xE0) {
                buf.pos += 2; // Skip 2 data bytes
            } else if (type == 0xC0 || type == 0xD0) {
                buf.pos += 1; // Skip 1 data byte
            } else if (status == 0xFF) {
                // Meta event
                if (buf.pos + 2 > buf.length) break;
                uint8_t metaType = buf.data[buf.pos++];
                uint32_t metaLen = readVarLen(&buf);
                if (metaType == 0x51 && metaLen == 3 && buf.pos + 3 <= buf.length) {
                    // Tempo
                    midi->tempo = (buf.data[buf.pos] << 16) |
                                  (buf.data[buf.pos + 1] << 8) |
                                  buf.data[buf.pos + 2];
                }
                buf.pos += metaLen;
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx
                uint32_t len = readVarLen(&buf);
                buf.pos += len;
            }
        }
        buf.pos = trackEnd;
    }

    free(data);

    // Sort events by tick
    if (midi->eventCount > 0) {
        qsort(midi->events, midi->eventCount, sizeof(MidiEvent), compareMidiEvents);
    }

    return PTR_VAL(midi);
}

// getMidiEventCount(midi) -> int
static Value getMidiEventCountNative(int argCount, Value* args) {
    (void)argCount;
    MidiFile* midi = (MidiFile*)AS_PTR(args[0]);
    if (!midi) return INT_VAL(0);
    return INT_VAL(midi->eventCount);
}

// getMidiEvent(midi, index) -> array [tick, status, note, velocity]
static Value getMidiEventNative(int argCount, Value* args) {
    (void)argCount;
    MidiFile* midi = (MidiFile*)AS_PTR(args[0]);
    int index = (int)AS_INT(args[1]);

    if (!midi || index < 0 || index >= midi->eventCount) return NIL_VAL;

    MidiEvent* ev = &midi->events[index];
    ObjArray* arr = newArray();
    writeArray(arr, INT_VAL(ev->tick));
    writeArray(arr, INT_VAL(ev->status));
    writeArray(arr, INT_VAL(ev->data1));
    writeArray(arr, INT_VAL(ev->data2));
    return OBJ_VAL(arr);
}

// getMidiTicksPerBeat(midi) -> int
static Value getMidiTicksPerBeatNative(int argCount, Value* args) {
    (void)argCount;
    MidiFile* midi = (MidiFile*)AS_PTR(args[0]);
    if (!midi) return INT_VAL(480);
    return INT_VAL(midi->ticksPerBeat);
}

// getMidiTempo(midi) -> int (microseconds per beat)
static Value getMidiTempoNative(int argCount, Value* args) {
    (void)argCount;
    MidiFile* midi = (MidiFile*)AS_PTR(args[0]);
    if (!midi) return INT_VAL(500000);
    return INT_VAL(midi->tempo);
}

// destroyMidi(midi)
static Value destroyMidiNative(int argCount, Value* args) {
    (void)argCount;
    MidiFile* midi = (MidiFile*)AS_PTR(args[0]);
    if (midi) {
        free(midi->events);
        free(midi);
    }
    return NIL_VAL;
}

// ============ String Native Functions ============

// chr(code) -> single character string
static Value chrNative(int argCount, Value* args) {
    (void)argCount;
    int code = (int)AS_INT(args[0]);
    char c = (char)code;
    return OBJ_VAL(copyString(&c, 1));
}

// str(value) -> string representation of any value
static Value strNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* result = valueToString(args[0]);
    return OBJ_VAL(result);
}

// substring(str, start, length) -> str
static Value substringNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    int start = (int)AS_INT(args[1]);
    int length = (int)AS_INT(args[2]);

    if (start < 0) start = 0;
    if (start >= str->length) return OBJ_VAL(copyString("", 0));
    if (length < 0) length = 0;
    if (start + length > str->length) length = str->length - start;

    return OBJ_VAL(copyString(str->chars + start, length));
}

// indexOf(str, search) -> int (-1 if not found)
static Value indexOfNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    ObjString* search = AS_STRING(args[1]);

    if (search->length == 0) return INT_VAL(0);
    if (search->length > str->length) return INT_VAL(-1);

    for (int i = 0; i <= str->length - search->length; i++) {
        if (memcmp(str->chars + i, search->chars, search->length) == 0) {
            return INT_VAL(i);
        }
    }
    return INT_VAL(-1);
}

// contains(str, search) -> bool
static Value containsNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    ObjString* search = AS_STRING(args[1]);

    if (search->length == 0) return BOOL_VAL(true);
    if (search->length > str->length) return BOOL_VAL(false);

    for (int i = 0; i <= str->length - search->length; i++) {
        if (memcmp(str->chars + i, search->chars, search->length) == 0) {
            return BOOL_VAL(true);
        }
    }
    return BOOL_VAL(false);
}

// startsWith(str, prefix) -> bool
static Value startsWithNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    ObjString* prefix = AS_STRING(args[1]);

    if (prefix->length > str->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(str->chars, prefix->chars, prefix->length) == 0);
}

// endsWith(str, suffix) -> bool
static Value endsWithNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    ObjString* suffix = AS_STRING(args[1]);

    if (suffix->length > str->length) return BOOL_VAL(false);
    return BOOL_VAL(memcmp(str->chars + str->length - suffix->length,
                           suffix->chars, suffix->length) == 0);
}

// split(str, delimiter) -> array of strings
static Value splitNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    ObjString* delim = AS_STRING(args[1]);

    ObjArray* result = newArray();
    push(OBJ_VAL(result)); // GC protection

    if (delim->length == 0) {
        // Split into characters
        for (int i = 0; i < str->length; i++) {
            ObjString* ch = copyString(str->chars + i, 1);
            writeArray(result, OBJ_VAL(ch));
        }
    } else {
        int start = 0;
        for (int i = 0; i <= str->length - delim->length; i++) {
            if (memcmp(str->chars + i, delim->chars, delim->length) == 0) {
                ObjString* part = copyString(str->chars + start, i - start);
                writeArray(result, OBJ_VAL(part));
                i += delim->length - 1;
                start = i + 1;
            }
        }
        // Add remaining
        ObjString* part = copyString(str->chars + start, str->length - start);
        writeArray(result, OBJ_VAL(part));
    }

    pop(); // GC protection
    return OBJ_VAL(result);
}

// trim(str) -> str (removes leading/trailing whitespace)
static Value trimNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);

    int start = 0;
    while (start < str->length && (str->chars[start] == ' ' ||
           str->chars[start] == '\t' || str->chars[start] == '\n' ||
           str->chars[start] == '\r')) {
        start++;
    }

    int end = str->length;
    while (end > start && (str->chars[end - 1] == ' ' ||
           str->chars[end - 1] == '\t' || str->chars[end - 1] == '\n' ||
           str->chars[end - 1] == '\r')) {
        end--;
    }

    return OBJ_VAL(copyString(str->chars + start, end - start));
}

// toUpper(str) -> str
static Value toUpperNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);

    char* buffer = malloc(str->length + 1);
    for (int i = 0; i < str->length; i++) {
        char c = str->chars[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        buffer[i] = c;
    }
    buffer[str->length] = '\0';

    ObjString* result = copyString(buffer, str->length);
    free(buffer);
    return OBJ_VAL(result);
}

// toLower(str) -> str
static Value toLowerNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);

    char* buffer = malloc(str->length + 1);
    for (int i = 0; i < str->length; i++) {
        char c = str->chars[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        buffer[i] = c;
    }
    buffer[str->length] = '\0';

    ObjString* result = copyString(buffer, str->length);
    free(buffer);
    return OBJ_VAL(result);
}

// replace(str, old, new) -> str
static Value replaceNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    ObjString* oldStr = AS_STRING(args[1]);
    ObjString* newStr = AS_STRING(args[2]);

    if (oldStr->length == 0) return OBJ_VAL(str);

    // Count occurrences
    int count = 0;
    for (int i = 0; i <= str->length - oldStr->length; i++) {
        if (memcmp(str->chars + i, oldStr->chars, oldStr->length) == 0) {
            count++;
            i += oldStr->length - 1;
        }
    }

    if (count == 0) return OBJ_VAL(str);

    // Allocate result
    int newLen = str->length + count * (newStr->length - oldStr->length);
    char* buffer = malloc(newLen + 1);
    char* dst = buffer;

    int i = 0;
    while (i < str->length) {
        if (i <= str->length - oldStr->length &&
            memcmp(str->chars + i, oldStr->chars, oldStr->length) == 0) {
            memcpy(dst, newStr->chars, newStr->length);
            dst += newStr->length;
            i += oldStr->length;
        } else {
            *dst++ = str->chars[i++];
        }
    }
    *dst = '\0';

    ObjString* result = copyString(buffer, newLen);
    free(buffer);
    return OBJ_VAL(result);
}

// charCodeAt(str, index) -> int
static Value charCodeAtNative(int argCount, Value* args) {
    (void)argCount;
    ObjString* str = AS_STRING(args[0]);
    int index = (int)AS_INT(args[1]);

    if (index < 0 || index >= str->length) return INT_VAL(-1);
    return INT_VAL((unsigned char)str->chars[index]);
}

// ============ File I/O Native Functions ============

// readFileNative(path) -> string or nil
static Value readFileNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return NIL_VAL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fclose(file);
        return NIL_VAL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);

    ObjString* result = copyString(buffer, (int)bytesRead);
    free(buffer);
    return OBJ_VAL(result);
}

// writeFileNative(path, content) -> bool
static Value writeFileNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);
    ObjString* content = AS_STRING(args[1]);

    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return BOOL_VAL(false);
    }

    size_t written = fwrite(content->chars, sizeof(char), content->length, file);
    fclose(file);

    return BOOL_VAL(written == (size_t)content->length);
}

// appendFileNative(path, content) -> bool
static Value appendFileNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);
    ObjString* content = AS_STRING(args[1]);

    FILE* file = fopen(path, "ab");
    if (file == NULL) {
        return BOOL_VAL(false);
    }

    size_t written = fwrite(content->chars, sizeof(char), content->length, file);
    fclose(file);

    return BOOL_VAL(written == (size_t)content->length);
}

// fileExistsNative(path) -> bool
static Value fileExistsNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    FILE* file = fopen(path, "r");
    if (file == NULL) {
        return BOOL_VAL(false);
    }
    fclose(file);
    return BOOL_VAL(true);
}

// listDir(path) -> array of filenames
static Value listDirNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    DIR* dir = opendir(path);
    if (dir == NULL) {
        // Return empty array on error
        ObjArray* arr = newArray();
        return OBJ_VAL(arr);
    }

    ObjArray* arr = newArray();
    push(OBJ_VAL(arr));  // GC protection

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcmp(entry->d_name, "..") == 0) continue;

        ObjString* name = copyString(entry->d_name, strlen(entry->d_name));
        writeArray(arr, OBJ_VAL(name));
    }

    closedir(dir);
    pop();  // Remove GC protection
    return OBJ_VAL(arr);
}

// isDir(path) -> bool
static Value isDirNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        return BOOL_VAL(false);
    }
    return BOOL_VAL(S_ISDIR(statbuf.st_mode));
}

// getFileSize(path) -> int (bytes, or -1 on error)
static Value getFileSizeNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        return INT_VAL(-1);
    }
    return INT_VAL((int64_t)statbuf.st_size);
}

// exec(command) -> bool (run command in background, non-blocking)
static Value execNative(int argCount, Value* args) {
    (void)argCount;
    const char* command = AS_CSTRING(args[0]);

    // Fork and exec in background
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - run command via shell
        setsid();  // Detach from parent
        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(127);  // exec failed
    } else if (pid > 0) {
        // Parent - don't wait, let child run independently
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

// getMimeType(path) -> str (get MIME type using file command)
static Value getMimeTypeNative(int argCount, Value* args) {
    (void)argCount;
    const char* path = AS_CSTRING(args[0]);

    char command[1024];
    snprintf(command, sizeof(command), "file --mime-type -b '%s' 2>/dev/null", path);

    FILE* fp = popen(command, "r");
    if (fp == NULL) {
        return OBJ_VAL(copyString("application/octet-stream", 24));
    }

    char result[256];
    if (fgets(result, sizeof(result), fp) != NULL) {
        // Remove trailing newline
        size_t len = strlen(result);
        if (len > 0 && result[len-1] == '\n') {
            result[len-1] = '\0';
            len--;
        }
        pclose(fp);
        return OBJ_VAL(copyString(result, len));
    }

    pclose(fp);
    return OBJ_VAL(copyString("application/octet-stream", 24));
}

static void defineNative(const char* name, NativeFn function) {
    push(OBJ_VAL(copyString(name, (int)strlen(name))));
    push(OBJ_VAL(newNative(function)));
    tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
    pop();
    pop();
}

void initVM(void) {
    resetStack();
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 1024;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;

    initTable(&vm.globals);
    initTable(&vm.strings);

    defineNative("clock", clockNative);
    defineNative("error", errorNative);
    defineNative("assert", assertNative);
    defineNative("typeof", typeNative);

    // SDL3 functions
    defineNative("init", initNative);
    defineNative("quit", quitNative);
    defineNative("createWindow", createWindowNative);
    defineNative("destroyWindow", destroyWindowNative);
    defineNative("createRenderer", createRendererNative);
    defineNative("setLogicalSize", setLogicalSizeNative);
    defineNative("destroyRenderer", destroyRendererNative);
    defineNative("clear", clearNative);
    defineNative("present", presentNative);
    defineNative("setDrawColor", setDrawColorNative);
    defineNative("setBlendMode", setBlendModeNative);
    defineNative("fillRect", fillRectNative);
    defineNative("drawRect", drawRectNative);
    defineNative("drawLine", drawLineNative);
    defineNative("pollEvent", pollEventNative);
    defineNative("eventKey", eventKeyNative);
    defineNative("isKeyDown", isKeyDownNative);
    defineNative("startTextInput", startTextInputNative);
    defineNative("stopTextInput", stopTextInputNative);
    defineNative("getTextInput", getTextInputNative);
    defineNative("eventWindowW", eventWindowWNative);
    defineNative("eventWindowH", eventWindowHNative);
    defineNative("getMouseX", getMouseXNative);
    defineNative("getMouseY", getMouseYNative);
    defineNative("getLogicalMouseX", getLogicalMouseXNative);
    defineNative("getLogicalMouseY", getLogicalMouseYNative);
    defineNative("getMouseButton", getMouseButtonNative);
    defineNative("getMouseWheelY", getMouseWheelYNative);
    defineNative("delay", delayNative);
    defineNative("getTicks", getTicksNative);
    defineNative("loadTexture", loadTextureNative);
    defineNative("destroyTexture", destroyTextureNative);
    defineNative("drawTexture", drawTextureNative);
    defineNative("getTextureSize", getTextureSizeNative);
    defineNative("random", randomNative);
    defineNative("randomFloat", randomFloatNative);
    // Math functions
    defineNative("sqrt", sqrtNative);
    defineNative("floor", floorNative);
    defineNative("trunc", intNative);
    defineNative("ceil", ceilNative);
    defineNative("round", roundNative);
    defineNative("sin", sinNative);
    defineNative("cos", cosNative);
    defineNative("tan", tanNative);
    defineNative("asin", asinNative);
    defineNative("acos", acosNative);
    defineNative("atan", atanNative);
    defineNative("atan2", atan2Native);
    defineNative("abs", absNative);
    defineNative("pow", powNative);
    defineNative("log", logNative);
    defineNative("exp", expNative);
    defineNative("min", minNative);
    defineNative("max", maxNative);

    // Math constants
    tableSet(&vm.globals, copyString("PI", 2), FLOAT_VAL(3.14159265358979323846));
    tableSet(&vm.globals, copyString("TAU", 3), FLOAT_VAL(6.28318530717958647693));
    tableSet(&vm.globals, copyString("E", 1), FLOAT_VAL(2.71828182845904523536));

    // TTF functions
    defineNative("initTTF", initTTFNative);
    defineNative("quitTTF", quitTTFNative);
    defineNative("loadFont", loadFontNative);
    defineNative("destroyFont", destroyFontNative);
    defineNative("drawText", drawTextNative);
    defineNative("getTextWidth", getTextWidthNative);

    // Array functions
    defineNative("len", lenNative);
    defineNative("push", pushNative);
    defineNative("pop", popNative);

    // TCP sockets
    defineNative("tcpListen", tcpListenNative);
    defineNative("tcpAccept", tcpAcceptNative);
    defineNative("tcpRecv", tcpRecvNative);
    defineNative("tcpSend", tcpSendNative);
    defineNative("tcpClose", tcpCloseNative);

    // String functions
    defineNative("chr", chrNative);
    defineNative("toString", strNative);
    defineNative("substring", substringNative);
    defineNative("substr", substringNative);  // alias
    defineNative("indexOf", indexOfNative);
    defineNative("contains", containsNative);
    defineNative("startsWith", startsWithNative);
    defineNative("endsWith", endsWithNative);
    defineNative("split", splitNative);
    defineNative("trim", trimNative);
    defineNative("toUpper", toUpperNative);
    defineNative("toLower", toLowerNative);
    defineNative("replace", replaceNative);
    defineNative("charCodeAt", charCodeAtNative);

    // File I/O functions
    defineNative("readFile", readFileNative);
    defineNative("writeFile", writeFileNative);
    defineNative("appendFile", appendFileNative);
    defineNative("fileExists", fileExistsNative);
    defineNative("listDir", listDirNative);
    defineNative("isDir", isDirNative);
    defineNative("getFileSize", getFileSizeNative);
    defineNative("exec", execNative);
    defineNative("getMimeType", getMimeTypeNative);

    // Audio functions
    defineNative("initAudio", initAudioNative);
    defineNative("loadSound", loadSoundNative);
    defineNative("playSound", playSoundNative);
    defineNative("destroySound", destroySoundNative);

    // Synthesizer functions
    defineNative("initSynth", initSynthNative);
    defineNative("noteOn", noteOnNative);
    defineNative("noteOff", noteOffNative);
    defineNative("allNotesOff", allNotesOffNative);
    defineNative("setSynthVolume", setSynthVolumeNative);

    // MIDI functions
    defineNative("loadMidi", loadMidiNative);
    defineNative("getMidiEventCount", getMidiEventCountNative);
    defineNative("getMidiEvent", getMidiEventNative);
    defineNative("getMidiTicksPerBeat", getMidiTicksPerBeatNative);
    defineNative("getMidiTempo", getMidiTempoNative);
    defineNative("destroyMidi", destroyMidiNative);
}

void freeVM(void) {
    freeTable(&vm.globals);
    freeTable(&vm.strings);
    freeObjects();
}

void push(Value value) {
    *vm.stackTop = value;
    vm.stackTop++;
}

Value pop(void) {
    vm.stackTop--;
    return *vm.stackTop;
}

Value peek(int distance) {
    return vm.stackTop[-1 - distance];
}

static ObjUpvalue* captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = NULL;
    ObjUpvalue* upvalue = vm.openUpvalues;

    while (upvalue != NULL && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        vm.openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(Value* last) {
    while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
        ObjUpvalue* upvalue = vm.openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm.openUpvalues = upvalue->next;
    }
}

static void concatenate(void) {
    ObjString* b = AS_STRING(peek(0));
    ObjString* a = AS_STRING(peek(1));

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    pop();
    pop();
    push(OBJ_VAL(result));
}

// Convert value to string for concatenation
static ObjString* valueToString(Value value) {
    char buffer[64];
    int len = 0;

    if (IS_INT(value)) {
        len = snprintf(buffer, sizeof(buffer), "%lld", (long long)AS_INT(value));
    } else if (IS_FLOAT(value)) {
        len = snprintf(buffer, sizeof(buffer), "%g", AS_FLOAT(value));
    } else if (IS_BOOL(value)) {
        len = snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        len = snprintf(buffer, sizeof(buffer), "nil");
    } else if (IS_STRING(value)) {
        return AS_STRING(value);
    } else {
        len = snprintf(buffer, sizeof(buffer), "<object>");
    }

    return copyString(buffer, len);
}

// Concatenate string with any value
static void concatenateAny(void) {
    Value bVal = peek(0);
    Value aVal = peek(1);

    ObjString* b = valueToString(bVal);
    push(OBJ_VAL(b)); // protect from GC
    ObjString* a = valueToString(aVal);

    int length = a->length + b->length;
    char* chars = ALLOCATE(char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    pop(); // pop GC protection
    pop(); // pop b
    pop(); // pop a
    push(OBJ_VAL(result));
}

static InterpretResult run(void) {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk->constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() \
    (frame->closure->function->chunk->constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_STRING_LONG() AS_STRING(READ_CONSTANT_LONG())

#define BINARY_OP_INT(op) \
    do { \
        if (!IS_INT(peek(0)) || !IS_INT(peek(1))) { \
            runtimeError("Operands must be integers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        int64_t b = AS_INT(pop()); \
        int64_t a = AS_INT(pop()); \
        push(INT_VAL(a op b)); \
    } while (false)

#define BINARY_OP_FLOAT(op) \
    do { \
        if (!IS_FLOAT(peek(0)) || !IS_FLOAT(peek(1))) { \
            runtimeError("Operands must be floats."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
        double b = AS_FLOAT(pop()); \
        double a = AS_FLOAT(pop()); \
        push(FLOAT_VAL(a op b)); \
    } while (false)

#define BINARY_OP_NUMERIC(valueType, op) \
    do { \
        if (IS_INT(peek(0)) && IS_INT(peek(1))) { \
            int64_t b = AS_INT(pop()); \
            int64_t a = AS_INT(pop()); \
            push(valueType(a op b)); \
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) { \
            double b = AS_NUMBER(pop()); \
            double a = AS_NUMBER(pop()); \
            push(valueType(a op b)); \
        } else { \
            runtimeError("Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
        } \
    } while (false)

#ifdef COMPUTED_GOTO
    // Dispatch table for computed goto - must match OpCode enum order
    static void* dispatch_table[] = {
        &&do_CONSTANT,       // OP_CONSTANT
        &&do_CONSTANT_LONG,  // OP_CONSTANT_LONG
        &&do_NIL,            // OP_NIL
        &&do_TRUE,           // OP_TRUE
        &&do_FALSE,          // OP_FALSE
        &&do_POP,            // OP_POP
        &&do_DUP,            // OP_DUP
        &&do_DUP_TWO,        // OP_DUP_TWO
        &&do_GET_LOCAL,      // OP_GET_LOCAL
        &&do_SET_LOCAL,      // OP_SET_LOCAL
        &&do_GET_GLOBAL,     // OP_GET_GLOBAL
        &&do_GET_GLOBAL_LONG, // OP_GET_GLOBAL_LONG
        &&do_DEFINE_GLOBAL,  // OP_DEFINE_GLOBAL
        &&do_DEFINE_GLOBAL_LONG, // OP_DEFINE_GLOBAL_LONG
        &&do_SET_GLOBAL,     // OP_SET_GLOBAL
        &&do_SET_GLOBAL_LONG, // OP_SET_GLOBAL_LONG
        &&do_GET_UPVALUE,    // OP_GET_UPVALUE
        &&do_SET_UPVALUE,    // OP_SET_UPVALUE
        &&do_UNUSED,         // OP_GET_PROPERTY (unused)
        &&do_UNUSED,         // OP_SET_PROPERTY (unused)
        &&do_EQUAL,          // OP_EQUAL
        &&do_NOT_EQUAL,      // OP_NOT_EQUAL
        &&do_GREATER,        // OP_GREATER
        &&do_GREATER_EQUAL,  // OP_GREATER_EQUAL
        &&do_LESS,           // OP_LESS
        &&do_LESS_EQUAL,     // OP_LESS_EQUAL
        &&do_ADD_INT,        // OP_ADD_INT
        &&do_SUBTRACT_INT,   // OP_SUBTRACT_INT
        &&do_MULTIPLY_INT,   // OP_MULTIPLY_INT
        &&do_DIVIDE_INT,     // OP_DIVIDE_INT
        &&do_MODULO_INT,     // OP_MODULO_INT
        &&do_NEGATE_INT,     // OP_NEGATE_INT
        &&do_ADD_FLOAT,      // OP_ADD_FLOAT
        &&do_SUBTRACT_FLOAT, // OP_SUBTRACT_FLOAT
        &&do_MULTIPLY_FLOAT, // OP_MULTIPLY_FLOAT
        &&do_DIVIDE_FLOAT,   // OP_DIVIDE_FLOAT
        &&do_NEGATE_FLOAT,   // OP_NEGATE_FLOAT
        &&do_ADD,            // OP_ADD
        &&do_SUBTRACT,       // OP_SUBTRACT
        &&do_MULTIPLY,       // OP_MULTIPLY
        &&do_DIVIDE,         // OP_DIVIDE
        &&do_MODULO,         // OP_MODULO
        &&do_NEGATE,         // OP_NEGATE
        &&do_NOT,            // OP_NOT
        &&do_INT_TO_FLOAT,   // OP_INT_TO_FLOAT
        &&do_FLOAT_TO_INT,   // OP_FLOAT_TO_INT
        &&do_JUMP,           // OP_JUMP
        &&do_JUMP_IF_FALSE,  // OP_JUMP_IF_FALSE
        &&do_LOOP,           // OP_LOOP
        &&do_CALL,           // OP_CALL
        &&do_CLOSURE,        // OP_CLOSURE
        &&do_CLOSE_UPVALUE,  // OP_CLOSE_UPVALUE
        &&do_RETURN,         // OP_RETURN
        &&do_UNUSED,         // OP_NATIVE_CALL (unused)
        &&do_PRINT,          // OP_PRINT
        &&do_STRUCT_DEF,     // OP_STRUCT_DEF
        &&do_STRUCT_FIELD,   // OP_STRUCT_FIELD
        &&do_UNUSED,         // OP_STRUCT_CALL (unused)
        &&do_GET_FIELD,      // OP_GET_FIELD
        &&do_GET_FIELD_LONG, // OP_GET_FIELD_LONG
        &&do_SET_FIELD,      // OP_SET_FIELD
        &&do_SET_FIELD_LONG, // OP_SET_FIELD_LONG
        &&do_ARRAY,          // OP_ARRAY
        &&do_INDEX_GET,      // OP_INDEX_GET
        &&do_INDEX_SET,      // OP_INDEX_SET
        &&do_METHOD,         // OP_METHOD
        &&do_INVOKE,         // OP_INVOKE
        &&do_UNUSED,         // OP_GET_SELF (unused)
        &&do_IMPORT,         // OP_IMPORT
        // Superinstructions
        &&do_GET_LOCAL_0,    // OP_GET_LOCAL_0
        &&do_GET_LOCAL_1,    // OP_GET_LOCAL_1
        &&do_GET_LOCAL_2,    // OP_GET_LOCAL_2
        &&do_GET_LOCAL_3,    // OP_GET_LOCAL_3
        &&do_INC_LOCAL,      // OP_INC_LOCAL
        &&do_ADD_LOCAL_CONST,    // OP_ADD_LOCAL_CONST
        &&do_LESS_LOCAL_CONST,   // OP_LESS_LOCAL_CONST
        &&do_INDEX_GET_LOCAL,    // OP_INDEX_GET_LOCAL
    };

#define DISPATCH() \
    do { \
        DEBUG_TRACE(); \
        goto *dispatch_table[READ_BYTE()]; \
    } while (false)

#ifdef DEBUG_TRACE_EXECUTION
#define DEBUG_TRACE() \
    do { \
        printf("          "); \
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) { \
            printf("[ "); \
            printValue(*slot); \
            printf(" ]"); \
        } \
        printf("\n"); \
        disassembleInstruction(frame->closure->function->chunk, \
            (int)(frame->ip - frame->closure->function->chunk->code)); \
    } while (false)
#else
#define DEBUG_TRACE() do {} while (false)
#endif

    // Start dispatch
    DISPATCH();

    do_UNUSED:
        runtimeError("Invalid opcode.");
        return INTERPRET_RUNTIME_ERROR;

    do_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        DISPATCH();
    }
    do_CONSTANT_LONG: {
        uint16_t index = READ_SHORT();
        Value constant = frame->closure->function->chunk->constants.values[index];
        push(constant);
        DISPATCH();
    }
    do_NIL:   push(NIL_VAL); DISPATCH();
    do_TRUE:  push(BOOL_VAL(true)); DISPATCH();
    do_FALSE: push(BOOL_VAL(false)); DISPATCH();
    do_POP:   pop(); DISPATCH();
    do_DUP:   push(peek(0)); DISPATCH();

    do_DUP_TWO: {
        Value b = peek(0);
        Value a = peek(1);
        push(a);
        push(b);
        DISPATCH();
    }

    do_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        DISPATCH();
    }
    do_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
        frame->slots[slot] = peek(0);
        DISPATCH();
    }
    do_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
            runtimeError("Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        DISPATCH();
    }
    do_GET_GLOBAL_LONG: {
        uint16_t index = READ_SHORT();
        ObjString* name = AS_STRING(frame->closure->function->chunk->constants.values[index]);
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
            runtimeError("Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        DISPATCH();
    }
    do_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, name, peek(0));
        pop();
        DISPATCH();
    }
    do_DEFINE_GLOBAL_LONG: {
        uint16_t index = READ_SHORT();
        ObjString* name = AS_STRING(frame->closure->function->chunk->constants.values[index]);
        tableSet(&vm.globals, name, peek(0));
        pop();
        DISPATCH();
    }
    do_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if (tableSet(&vm.globals, name, peek(0))) {
            tableDelete(&vm.globals, name);
            runtimeError("Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    do_SET_GLOBAL_LONG: {
        uint16_t index = READ_SHORT();
        ObjString* name = AS_STRING(frame->closure->function->chunk->constants.values[index]);
        if (tableSet(&vm.globals, name, peek(0))) {
            tableDelete(&vm.globals, name);
            runtimeError("Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    do_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        push(*frame->closure->upvalues[slot]->location);
        DISPATCH();
    }
    do_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        *frame->closure->upvalues[slot]->location = peek(0);
        DISPATCH();
    }

    do_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        DISPATCH();
    }
    do_NOT_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(!valuesEqual(a, b)));
        DISPATCH();
    }
    do_GREATER:       BINARY_OP_NUMERIC(BOOL_VAL, >); DISPATCH();
    do_GREATER_EQUAL: BINARY_OP_NUMERIC(BOOL_VAL, >=); DISPATCH();
    do_LESS:          BINARY_OP_NUMERIC(BOOL_VAL, <); DISPATCH();
    do_LESS_EQUAL:    BINARY_OP_NUMERIC(BOOL_VAL, <=); DISPATCH();

    do_ADD_INT:      BINARY_OP_INT(+); DISPATCH();
    do_SUBTRACT_INT: BINARY_OP_INT(-); DISPATCH();
    do_MULTIPLY_INT: BINARY_OP_INT(*); DISPATCH();
    do_DIVIDE_INT:   BINARY_OP_INT(/); DISPATCH();
    do_MODULO_INT:   BINARY_OP_INT(%); DISPATCH();
    do_NEGATE_INT: {
        if (!IS_INT(peek(0))) {
            runtimeError("Operand must be an integer.");
            return INTERPRET_RUNTIME_ERROR;
        }
        push(INT_VAL(-AS_INT(pop())));
        DISPATCH();
    }

    do_ADD_FLOAT:      BINARY_OP_FLOAT(+); DISPATCH();
    do_SUBTRACT_FLOAT: BINARY_OP_FLOAT(-); DISPATCH();
    do_MULTIPLY_FLOAT: BINARY_OP_FLOAT(*); DISPATCH();
    do_DIVIDE_FLOAT:   BINARY_OP_FLOAT(/); DISPATCH();
    do_NEGATE_FLOAT: {
        if (!IS_FLOAT(peek(0))) {
            runtimeError("Operand must be a float.");
            return INTERPRET_RUNTIME_ERROR;
        }
        push(FLOAT_VAL(-AS_FLOAT(pop())));
        DISPATCH();
    }

    do_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
            concatenate();
        } else if (IS_STRING(peek(0)) || IS_STRING(peek(1))) {
            concatenateAny();
        } else if (IS_INT(peek(0)) && IS_INT(peek(1))) {
            int64_t b = AS_INT(pop());
            int64_t a = AS_INT(pop());
            push(INT_VAL(a + b));
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
            double b = AS_NUMBER(pop());
            double a = AS_NUMBER(pop());
            push(FLOAT_VAL(a + b));
        } else {
            runtimeError("Operands must be two numbers or two strings.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    do_SUBTRACT: {
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
            int64_t b = AS_INT(pop());
            int64_t a = AS_INT(pop());
            push(INT_VAL(a - b));
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
            double b = AS_NUMBER(pop());
            double a = AS_NUMBER(pop());
            push(FLOAT_VAL(a - b));
        } else {
            runtimeError("Operands must be numbers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    do_MULTIPLY: {
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
            int64_t b = AS_INT(pop());
            int64_t a = AS_INT(pop());
            push(INT_VAL(a * b));
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
            double b = AS_NUMBER(pop());
            double a = AS_NUMBER(pop());
            push(FLOAT_VAL(a * b));
        } else {
            runtimeError("Operands must be numbers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    do_DIVIDE: {
        if (IS_INT(peek(0)) && IS_INT(peek(1))) {
            int64_t b = AS_INT(pop());
            int64_t a = AS_INT(pop());
            if (b == 0) {
                runtimeError("Division by zero.");
                return INTERPRET_RUNTIME_ERROR;
            }
            push(INT_VAL(a / b));
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
            double b = AS_NUMBER(pop());
            double a = AS_NUMBER(pop());
            push(FLOAT_VAL(a / b));
        } else {
            runtimeError("Operands must be numbers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }
    do_MODULO: {
        if (!IS_INT(peek(0)) || !IS_INT(peek(1))) {
            runtimeError("Operands must be integers for modulo.");
            return INTERPRET_RUNTIME_ERROR;
        }
        int64_t b = AS_INT(pop());
        int64_t a = AS_INT(pop());
        if (b == 0) {
            runtimeError("Division by zero.");
            return INTERPRET_RUNTIME_ERROR;
        }
        push(INT_VAL(a % b));
        DISPATCH();
    }
    do_NEGATE: {
        if (IS_INT(peek(0))) {
            push(INT_VAL(-AS_INT(pop())));
        } else if (IS_FLOAT(peek(0))) {
            push(FLOAT_VAL(-AS_FLOAT(pop())));
        } else {
            runtimeError("Operand must be a number.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    do_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        DISPATCH();

    do_INT_TO_FLOAT: {
        if (!IS_INT(peek(0))) {
            runtimeError("Expected integer for conversion.");
            return INTERPRET_RUNTIME_ERROR;
        }
        push(FLOAT_VAL((double)AS_INT(pop())));
        DISPATCH();
    }
    do_FLOAT_TO_INT: {
        if (!IS_FLOAT(peek(0))) {
            runtimeError("Expected float for conversion.");
            return INTERPRET_RUNTIME_ERROR;
        }
        push(INT_VAL((int64_t)AS_FLOAT(pop())));
        DISPATCH();
    }

    do_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        DISPATCH();
    }
    do_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) frame->ip += offset;
        DISPATCH();
    }
    do_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        DISPATCH();
    }

    do_CALL: {
        int argCount = READ_BYTE();
        Value callee = peek(argCount);
        if (IS_NATIVE(callee)) {
            NativeFn native = AS_NATIVE(callee);
            Value result = native(argCount, vm.stackTop - argCount);
            vm.stackTop -= argCount + 1;
            push(result);
        } else if (IS_CLOSURE(callee)) {
            ObjClosure* closure = AS_CLOSURE(callee);
            if (argCount != closure->function->arity) {
                runtimeError("Expected %d arguments but got %d.",
                             closure->function->arity, argCount);
                return INTERPRET_RUNTIME_ERROR;
            }
            if (vm.frameCount == FRAMES_MAX) {
                runtimeError("Stack overflow.");
                return INTERPRET_RUNTIME_ERROR;
            }
            CallFrame* newFrame = &vm.frames[vm.frameCount++];
            newFrame->closure = closure;
            newFrame->ip = closure->function->chunk->code;
            newFrame->slots = vm.stackTop - argCount - 1;
            frame = newFrame;
        } else if (IS_STRUCT_DEF(callee)) {
            ObjStructDef* def = AS_STRUCT_DEF(callee);
            if (argCount != def->fieldCount) {
                runtimeError("Expected %d arguments but got %d.",
                             def->fieldCount, argCount);
                return INTERPRET_RUNTIME_ERROR;
            }
            ObjStruct* instance = newStruct(def);
            for (int i = 0; i < argCount; i++) {
                instance->fields[i] = peek(argCount - 1 - i);
            }
            vm.stackTop -= argCount + 1;
            push(OBJ_VAL(instance));
        } else if (IS_BOUND_METHOD(callee)) {
            ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
            ObjClosure* closure = bound->method;
            if (argCount != closure->function->arity) {
                runtimeError("Expected %d arguments but got %d.",
                             closure->function->arity, argCount);
                return INTERPRET_RUNTIME_ERROR;
            }
            if (vm.frameCount == FRAMES_MAX) {
                runtimeError("Stack overflow.");
                return INTERPRET_RUNTIME_ERROR;
            }
            vm.stackTop[-argCount - 1] = bound->receiver;
            CallFrame* newFrame = &vm.frames[vm.frameCount++];
            newFrame->closure = closure;
            newFrame->ip = closure->function->chunk->code;
            newFrame->slots = vm.stackTop - argCount - 1;
            frame = newFrame;
        } else {
            runtimeError("Can only call functions.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    do_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
            uint8_t isLocal = READ_BYTE();
            uint8_t index = READ_BYTE();
            if (isLocal) {
                closure->upvalues[i] = captureUpvalue(frame->slots + index);
            } else {
                closure->upvalues[i] = frame->closure->upvalues[index];
            }
        }
        DISPATCH();
    }

    do_CLOSE_UPVALUE:
        closeUpvalues(vm.stackTop - 1);
        pop();
        DISPATCH();

    do_RETURN: {
        Value result = pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
            pop();
            return INTERPRET_OK;
        }
        vm.stackTop = frame->slots;
        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        DISPATCH();
    }

    do_PRINT: {
        printValue(pop());
        printf("\n");
        DISPATCH();
    }

    do_ARRAY: {
        int count = READ_BYTE();
        ObjArray* array = newArray();
        push(OBJ_VAL(array));
        for (int i = count - 1; i >= 0; i--) {
            writeArray(array, peek(i + 1));
        }
        vm.stackTop -= count + 1;
        push(OBJ_VAL(array));
        DISPATCH();
    }

    do_INDEX_GET: {
        Value indexVal = pop();
        Value arrayVal = pop();
        if (!IS_ARRAY(arrayVal)) {
            runtimeError("Can only index arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_INT(indexVal)) {
            runtimeError("Array index must be an integer.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjArray* array = AS_ARRAY(arrayVal);
        int64_t index = AS_INT(indexVal);
        if (index < 0 || index >= array->count) {
            runtimeError("Array index %lld out of bounds [0, %d).",
                         (long long)index, array->count);
            return INTERPRET_RUNTIME_ERROR;
        }
        push(array->elements[index]);
        DISPATCH();
    }

    do_INDEX_SET: {
        Value value = pop();
        Value indexVal = pop();
        Value arrayVal = pop();
        if (!IS_ARRAY(arrayVal)) {
            runtimeError("Can only index arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }
        if (!IS_INT(indexVal)) {
            runtimeError("Array index must be an integer.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjArray* array = AS_ARRAY(arrayVal);
        int64_t index = AS_INT(indexVal);
        if (index < 0 || index >= array->count) {
            runtimeError("Array index %lld out of bounds [0, %d).",
                         (long long)index, array->count);
            return INTERPRET_RUNTIME_ERROR;
        }
        array->elements[index] = value;
        push(value);
        DISPATCH();
    }

    do_STRUCT_DEF: {
        int fieldCount = READ_BYTE();
        ObjString* name = READ_STRING();
        ObjStructDef* def = newStructDef(name);
        def->fieldCount = fieldCount;
        def->fieldNames = ALLOCATE(ObjString*, fieldCount);
        for (int i = 0; i < fieldCount; i++) {
            def->fieldNames[i] = NULL;
        }
        push(OBJ_VAL(def));
        DISPATCH();
    }

    do_STRUCT_FIELD: {
        ObjString* fieldName = READ_STRING();
        ObjStructDef* def = AS_STRUCT_DEF(peek(0));
        for (int i = 0; i < def->fieldCount; i++) {
            if (def->fieldNames[i] == NULL) {
                def->fieldNames[i] = fieldName;
                // O(1) lookup table: fieldName -> index
                tableSet(&def->fieldIndices, fieldName, INT_VAL(i));
                break;
            }
        }
        DISPATCH();
    }

    do_GET_FIELD: {
        if (!IS_STRUCT(peek(0))) {
            runtimeError("Only struct instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        Value receiver = peek(0);
        ObjStruct* instance = AS_STRUCT(receiver);
        ObjString* name = READ_STRING();
        int fieldIndex = -1;
        // Linear search for small structs (faster), hash table for large
        if (instance->definition->fieldCount <= 8) {
            for (int i = 0; i < instance->definition->fieldCount; i++) {
                if (instance->definition->fieldNames[i] == name) {
                    fieldIndex = i;
                    break;
                }
            }
        } else {
            Value indexVal;
            if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                fieldIndex = AS_INT(indexVal);
            }
        }
        if (fieldIndex != -1) {
            pop();
            push(instance->fields[fieldIndex]);
            DISPATCH();
        }
        // Check methods if not a field
        Value method;
        if (tableGet(&instance->definition->methods, name, &method)) {
            pop();
            ObjBoundMethod* bound = newBoundMethod(receiver, AS_CLOSURE(method));
            push(OBJ_VAL(bound));
            DISPATCH();
        }
        runtimeError("Undefined property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    do_GET_FIELD_LONG: {
        if (!IS_STRUCT(peek(0))) {
            runtimeError("Only struct instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        Value receiver = peek(0);
        ObjStruct* instance = AS_STRUCT(receiver);
        ObjString* name = READ_STRING_LONG();
        int fieldIndex = -1;
        if (instance->definition->fieldCount <= 8) {
            for (int i = 0; i < instance->definition->fieldCount; i++) {
                if (instance->definition->fieldNames[i] == name) {
                    fieldIndex = i;
                    break;
                }
            }
        } else {
            Value indexVal;
            if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                fieldIndex = AS_INT(indexVal);
            }
        }
        if (fieldIndex != -1) {
            pop();
            push(instance->fields[fieldIndex]);
            DISPATCH();
        }
        Value method;
        if (tableGet(&instance->definition->methods, name, &method)) {
            pop();
            ObjBoundMethod* bound = newBoundMethod(receiver, AS_CLOSURE(method));
            push(OBJ_VAL(bound));
            DISPATCH();
        }
        runtimeError("Undefined property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
    }

    do_SET_FIELD: {
        if (!IS_STRUCT(peek(1))) {
            runtimeError("Only struct instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjStruct* instance = AS_STRUCT(peek(1));
        ObjString* name = READ_STRING();
        Value value = pop();
        int fieldIndex = -1;
        // Linear search for small structs (faster), hash table for large
        if (instance->definition->fieldCount <= 8) {
            for (int i = 0; i < instance->definition->fieldCount; i++) {
                if (instance->definition->fieldNames[i] == name) {
                    fieldIndex = i;
                    break;
                }
            }
        } else {
            Value indexVal;
            if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                fieldIndex = AS_INT(indexVal);
            }
        }
        if (fieldIndex == -1) {
            runtimeError("Undefined field '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        instance->fields[fieldIndex] = value;
        pop();
        push(value);
        DISPATCH();
    }

    do_SET_FIELD_LONG: {
        if (!IS_STRUCT(peek(1))) {
            runtimeError("Only struct instances have fields.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjStruct* instance = AS_STRUCT(peek(1));
        ObjString* name = READ_STRING_LONG();
        Value value = pop();
        int fieldIndex = -1;
        if (instance->definition->fieldCount <= 8) {
            for (int i = 0; i < instance->definition->fieldCount; i++) {
                if (instance->definition->fieldNames[i] == name) {
                    fieldIndex = i;
                    break;
                }
            }
        } else {
            Value indexVal;
            if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                fieldIndex = AS_INT(indexVal);
            }
        }
        if (fieldIndex == -1) {
            runtimeError("Undefined field '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        instance->fields[fieldIndex] = value;
        pop();
        push(value);
        DISPATCH();
    }

    do_METHOD: {
        ObjString* name = READ_STRING();
        Value method = peek(0);
        ObjStructDef* def = AS_STRUCT_DEF(peek(1));
        tableSet(&def->methods, name, method);
        pop();
        DISPATCH();
    }

    do_INVOKE: {
        ObjString* methodName = READ_STRING();
        int argCount = READ_BYTE();
        Value receiver = peek(argCount);
        if (!IS_STRUCT(receiver)) {
            runtimeError("Only struct instances have methods.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjStruct* instance = AS_STRUCT(receiver);
        Value method;
        if (!tableGet(&instance->definition->methods, methodName, &method)) {
            runtimeError("Undefined method '%s'.", methodName->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjClosure* closure = AS_CLOSURE(method);
        if (argCount != closure->function->arity) {
            runtimeError("Expected %d arguments but got %d.",
                closure->function->arity, argCount);
            return INTERPRET_RUNTIME_ERROR;
        }
        if (vm.frameCount == FRAMES_MAX) {
            runtimeError("Stack overflow.");
            return INTERPRET_RUNTIME_ERROR;
        }
        CallFrame* newFrame = &vm.frames[vm.frameCount++];
        newFrame->closure = closure;
        newFrame->ip = closure->function->chunk->code;
        newFrame->slots = vm.stackTop - argCount - 1;
        frame = newFrame;
        DISPATCH();
    }

    do_IMPORT: {
        ObjString* path = READ_STRING();
        char* source = readFile(path->chars);
        if (source == NULL) {
            runtimeError("Could not open module '%s'.", path->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjFunction* moduleFunc = compile(source);
        free(source);
        if (moduleFunc == NULL) {
            runtimeError("Error compiling module '%s'.", path->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        push(OBJ_VAL(moduleFunc));
        ObjClosure* moduleClosure = newClosure(moduleFunc);
        pop();
        push(OBJ_VAL(moduleClosure));
        CallFrame* moduleFrame = &vm.frames[vm.frameCount++];
        moduleFrame->closure = moduleClosure;
        moduleFrame->ip = moduleFunc->chunk->code;
        moduleFrame->slots = vm.stackTop - 1;
        frame = moduleFrame;
        DISPATCH();
    }

    // === Superinstructions ===

    // Single-byte local access (no operand byte needed)
    do_GET_LOCAL_0: push(frame->slots[0]); DISPATCH();
    do_GET_LOCAL_1: push(frame->slots[1]); DISPATCH();
    do_GET_LOCAL_2: push(frame->slots[2]); DISPATCH();
    do_GET_LOCAL_3: push(frame->slots[3]); DISPATCH();

    // Increment local by 1: local = local + 1
    do_INC_LOCAL: {
        uint8_t slot = READ_BYTE();
        Value val = frame->slots[slot];
        if (IS_INT(val)) {
            frame->slots[slot] = INT_VAL(AS_INT(val) + 1);
        } else if (IS_FLOAT(val)) {
            frame->slots[slot] = FLOAT_VAL(AS_FLOAT(val) + 1.0);
        } else {
            runtimeError("Operand must be a number.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    // Load local + constant, add: push(local[slot] + const[idx])
    do_ADD_LOCAL_CONST: {
        uint8_t slot = READ_BYTE();
        Value constant = READ_CONSTANT();
        Value local = frame->slots[slot];
        // Fast path for ints
        if (IS_INT(local) && IS_INT(constant)) {
            push(INT_VAL(AS_INT(local) + AS_INT(constant)));
        } else if (IS_NUMBER(local) && IS_NUMBER(constant)) {
            push(FLOAT_VAL(AS_NUMBER(local) + AS_NUMBER(constant)));
        } else {
            runtimeError("Operands must be numbers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    // Compare local < constant: push(local[slot] < const[idx])
    do_LESS_LOCAL_CONST: {
        uint8_t slot = READ_BYTE();
        Value constant = READ_CONSTANT();
        Value local = frame->slots[slot];
        if (IS_NUMBER(local) && IS_NUMBER(constant)) {
            push(BOOL_VAL(AS_NUMBER(local) < AS_NUMBER(constant)));
        } else {
            runtimeError("Operands must be numbers.");
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

    // Array index with local: push(stack[-1][local[slot]])
    do_INDEX_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
        Value arrayVal = pop();
        if (!IS_ARRAY(arrayVal)) {
            runtimeError("Can only index arrays.");
            return INTERPRET_RUNTIME_ERROR;
        }
        Value indexVal = frame->slots[slot];
        if (!IS_INT(indexVal)) {
            runtimeError("Array index must be an integer.");
            return INTERPRET_RUNTIME_ERROR;
        }
        ObjArray* array = AS_ARRAY(arrayVal);
        int64_t index = AS_INT(indexVal);
        if (index < 0 || index >= array->count) {
            runtimeError("Array index %lld out of bounds [0, %d).",
                         (long long)index, array->count);
            return INTERPRET_RUNTIME_ERROR;
        }
        push(array->elements[index]);
        DISPATCH();
    }

#undef DISPATCH
#undef DEBUG_TRACE

#else // !COMPUTED_GOTO - Fall back to switch dispatch

    for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
        printf("          ");
        for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");
        disassembleInstruction(frame->closure->function->chunk,
                               (int)(frame->ip - frame->closure->function->chunk->code));
#endif

        uint8_t instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OP_CONSTANT_LONG: {
                uint16_t index = READ_SHORT();
                Value constant = frame->closure->function->chunk->constants.values[index];
                push(constant);
                break;
            }
            case OP_NIL:   push(NIL_VAL); break;
            case OP_TRUE:  push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;

            case OP_POP: pop(); break;
            case OP_DUP: push(peek(0)); break;
            case OP_DUP_TWO: {
                Value b = peek(0);
                Value a = peek(1);
                push(a);
                push(b);
                break;
            }

            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_GET_GLOBAL_LONG: {
                uint16_t index = READ_SHORT();
                ObjString* name = AS_STRING(frame->closure->function->chunk->constants.values[index]);
                Value value;
                if (!tableGet(&vm.globals, name, &value)) {
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_DEFINE_GLOBAL_LONG: {
                uint16_t index = READ_SHORT();
                ObjString* name = AS_STRING(frame->closure->function->chunk->constants.values[index]);
                tableSet(&vm.globals, name, peek(0));
                pop();
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SET_GLOBAL_LONG: {
                uint16_t index = READ_SHORT();
                ObjString* name = AS_STRING(frame->closure->function->chunk->constants.values[index]);
                if (tableSet(&vm.globals, name, peek(0))) {
                    tableDelete(&vm.globals, name);
                    runtimeError("Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_NOT_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(BOOL_VAL(!valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:       BINARY_OP_NUMERIC(BOOL_VAL, >); break;
            case OP_GREATER_EQUAL: BINARY_OP_NUMERIC(BOOL_VAL, >=); break;
            case OP_LESS:          BINARY_OP_NUMERIC(BOOL_VAL, <); break;
            case OP_LESS_EQUAL:    BINARY_OP_NUMERIC(BOOL_VAL, <=); break;

            case OP_ADD_INT:      BINARY_OP_INT(+); break;
            case OP_SUBTRACT_INT: BINARY_OP_INT(-); break;
            case OP_MULTIPLY_INT: BINARY_OP_INT(*); break;
            case OP_DIVIDE_INT:   BINARY_OP_INT(/); break;
            case OP_MODULO_INT:   BINARY_OP_INT(%); break;
            case OP_NEGATE_INT: {
                if (!IS_INT(peek(0))) {
                    runtimeError("Operand must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(INT_VAL(-AS_INT(pop())));
                break;
            }

            case OP_ADD_FLOAT:      BINARY_OP_FLOAT(+); break;
            case OP_SUBTRACT_FLOAT: BINARY_OP_FLOAT(-); break;
            case OP_MULTIPLY_FLOAT: BINARY_OP_FLOAT(*); break;
            case OP_DIVIDE_FLOAT:   BINARY_OP_FLOAT(/); break;
            case OP_NEGATE_FLOAT: {
                if (!IS_FLOAT(peek(0))) {
                    runtimeError("Operand must be a float.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(FLOAT_VAL(-AS_FLOAT(pop())));
                break;
            }

            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    concatenate();
                } else if (IS_STRING(peek(0)) || IS_STRING(peek(1))) {
                    concatenateAny();
                } else if (IS_INT(peek(0)) && IS_INT(peek(1))) {
                    int64_t b = AS_INT(pop());
                    int64_t a = AS_INT(pop());
                    push(INT_VAL(a + b));
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(FLOAT_VAL(a + b));
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUBTRACT: {
                if (IS_INT(peek(0)) && IS_INT(peek(1))) {
                    int64_t b = AS_INT(pop());
                    int64_t a = AS_INT(pop());
                    push(INT_VAL(a - b));
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(FLOAT_VAL(a - b));
                } else {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_MULTIPLY: {
                if (IS_INT(peek(0)) && IS_INT(peek(1))) {
                    int64_t b = AS_INT(pop());
                    int64_t a = AS_INT(pop());
                    push(INT_VAL(a * b));
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(FLOAT_VAL(a * b));
                } else {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_DIVIDE: {
                if (IS_INT(peek(0)) && IS_INT(peek(1))) {
                    int64_t b = AS_INT(pop());
                    int64_t a = AS_INT(pop());
                    if (b == 0) {
                        runtimeError("Division by zero.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    push(INT_VAL(a / b));
                } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(FLOAT_VAL(a / b));
                } else {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_MODULO: {
                if (!IS_INT(peek(0)) || !IS_INT(peek(1))) {
                    runtimeError("Operands must be integers for modulo.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                int64_t b = AS_INT(pop());
                int64_t a = AS_INT(pop());
                if (b == 0) {
                    runtimeError("Division by zero.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(INT_VAL(a % b));
                break;
            }
            case OP_NEGATE: {
                if (IS_INT(peek(0))) {
                    push(INT_VAL(-AS_INT(pop())));
                } else if (IS_FLOAT(peek(0))) {
                    push(FLOAT_VAL(-AS_FLOAT(pop())));
                } else {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_NOT:
                push(BOOL_VAL(isFalsey(pop())));
                break;

            case OP_INT_TO_FLOAT: {
                if (!IS_INT(peek(0))) {
                    runtimeError("Expected integer for conversion.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(FLOAT_VAL((double)AS_INT(pop())));
                break;
            }
            case OP_FLOAT_TO_INT: {
                if (!IS_FLOAT(peek(0))) {
                    runtimeError("Expected float for conversion.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(INT_VAL((int64_t)AS_FLOAT(pop())));
                break;
            }

            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }

            case OP_CALL: {
                int argCount = READ_BYTE();
                Value callee = peek(argCount);
                if (IS_NATIVE(callee)) {
                    NativeFn native = AS_NATIVE(callee);
                    Value result = native(argCount, vm.stackTop - argCount);
                    vm.stackTop -= argCount + 1;
                    push(result);
                } else if (IS_CLOSURE(callee)) {
                    ObjClosure* closure = AS_CLOSURE(callee);
                    if (argCount != closure->function->arity) {
                        runtimeError("Expected %d arguments but got %d.",
                                     closure->function->arity, argCount);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    if (vm.frameCount == FRAMES_MAX) {
                        runtimeError("Stack overflow.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    CallFrame* newFrame = &vm.frames[vm.frameCount++];
                    newFrame->closure = closure;
                    newFrame->ip = closure->function->chunk->code;
                    newFrame->slots = vm.stackTop - argCount - 1;
                    frame = newFrame;
                } else if (IS_STRUCT_DEF(callee)) {
                    // Struct constructor call: Point(10, 20)
                    ObjStructDef* def = AS_STRUCT_DEF(callee);
                    if (argCount != def->fieldCount) {
                        runtimeError("Expected %d arguments but got %d.",
                                     def->fieldCount, argCount);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    ObjStruct* instance = newStruct(def);
                    // Fill fields from stack args
                    for (int i = 0; i < argCount; i++) {
                        instance->fields[i] = peek(argCount - 1 - i);
                    }
                    // Pop args and struct def, push instance
                    vm.stackTop -= argCount + 1;
                    push(OBJ_VAL(instance));
                } else if (IS_BOUND_METHOD(callee)) {
                    // Bound method call
                    ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
                    ObjClosure* closure = bound->method;
                    if (argCount != closure->function->arity) {
                        runtimeError("Expected %d arguments but got %d.",
                                     closure->function->arity, argCount);
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    if (vm.frameCount == FRAMES_MAX) {
                        runtimeError("Stack overflow.");
                        return INTERPRET_RUNTIME_ERROR;
                    }
                    // Put receiver in slot 0
                    vm.stackTop[-argCount - 1] = bound->receiver;
                    CallFrame* newFrame = &vm.frames[vm.frameCount++];
                    newFrame->closure = closure;
                    newFrame->ip = closure->function->chunk->code;
                    newFrame->slots = vm.stackTop - argCount - 1;
                    frame = newFrame;
                } else {
                    runtimeError("Can only call functions.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(OBJ_VAL(closure));
                for (int i = 0; i < closure->upvalueCount; i++) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }

            case OP_GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }

            case OP_SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }

            case OP_CLOSE_UPVALUE:
                closeUpvalues(vm.stackTop - 1);
                pop();
                break;

            case OP_PRINT: {
                printValue(pop());
                printf("\n");
                break;
            }

            case OP_ARRAY: {
                int count = READ_BYTE();
                ObjArray* array = newArray();
                // Push array first to protect from GC
                push(OBJ_VAL(array));

                // Copy elements from stack to array (in correct order)
                // Elements are on stack: [elem0, elem1, ..., elemN-1, array]
                // peek(count) is elem0, peek(1) is elemN-1
                for (int i = count - 1; i >= 0; i--) {
                    writeArray(array, peek(i + 1));
                }

                // Pop the elements and the temporary array ref
                vm.stackTop -= count + 1;
                push(OBJ_VAL(array));
                break;
            }

            case OP_INDEX_GET: {
                Value indexVal = pop();
                Value arrayVal = pop();

                if (!IS_ARRAY(arrayVal)) {
                    runtimeError("Can only index arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_INT(indexVal)) {
                    runtimeError("Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* array = AS_ARRAY(arrayVal);
                int64_t index = AS_INT(indexVal);

                if (index < 0 || index >= array->count) {
                    runtimeError("Array index %lld out of bounds [0, %d).",
                                 (long long)index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                push(array->elements[index]);
                break;
            }

            case OP_INDEX_SET: {
                Value value = pop();
                Value indexVal = pop();
                Value arrayVal = pop();

                if (!IS_ARRAY(arrayVal)) {
                    runtimeError("Can only index arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                if (!IS_INT(indexVal)) {
                    runtimeError("Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjArray* array = AS_ARRAY(arrayVal);
                int64_t index = AS_INT(indexVal);

                if (index < 0 || index >= array->count) {
                    runtimeError("Array index %lld out of bounds [0, %d).",
                                 (long long)index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }

                array->elements[index] = value;
                push(value);
                break;
            }

            case OP_STRUCT_DEF: {
                int fieldCount = READ_BYTE();
                ObjString* name = READ_STRING();
                ObjStructDef* def = newStructDef(name);
                def->fieldCount = fieldCount;
                def->fieldNames = ALLOCATE(ObjString*, fieldCount);
                // Initialize all field names to NULL
                for (int i = 0; i < fieldCount; i++) {
                    def->fieldNames[i] = NULL;
                }
                // Field names will be added by OP_STRUCT_FIELD
                push(OBJ_VAL(def));
                break;
            }

            case OP_STRUCT_FIELD: {
                ObjString* fieldName = READ_STRING();
                ObjStructDef* def = AS_STRUCT_DEF(peek(0));
                // Find next empty slot in fieldNames
                for (int i = 0; i < def->fieldCount; i++) {
                    if (def->fieldNames[i] == NULL) {
                        def->fieldNames[i] = fieldName;
                        // O(1) lookup table: fieldName -> index
                        tableSet(&def->fieldIndices, fieldName, INT_VAL(i));
                        break;
                    }
                }
                break;
            }

            case OP_GET_FIELD: {
                if (!IS_STRUCT(peek(0))) {
                    runtimeError("Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                Value receiver = peek(0);
                ObjStruct* instance = AS_STRUCT(receiver);
                ObjString* name = READ_STRING();

                int fieldIndex = -1;
                // Linear search for small structs (faster), hash table for large
                if (instance->definition->fieldCount <= 8) {
                    for (int i = 0; i < instance->definition->fieldCount; i++) {
                        if (instance->definition->fieldNames[i] == name) {
                            fieldIndex = i;
                            break;
                        }
                    }
                } else {
                    Value indexVal;
                    if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                        fieldIndex = AS_INT(indexVal);
                    }
                }
                if (fieldIndex != -1) {
                    pop(); // pop instance
                    push(instance->fields[fieldIndex]);
                    break;
                }

                // Not a field - try to find a method
                Value method;
                if (tableGet(&instance->definition->methods, name, &method)) {
                    pop(); // pop instance
                    ObjBoundMethod* bound = newBoundMethod(receiver, AS_CLOSURE(method));
                    push(OBJ_VAL(bound));
                    break;
                }

                runtimeError("Undefined property '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }

            case OP_GET_FIELD_LONG: {
                if (!IS_STRUCT(peek(0))) {
                    runtimeError("Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value receiver = peek(0);
                ObjStruct* instance = AS_STRUCT(receiver);
                ObjString* name = READ_STRING_LONG();
                int fieldIndex = -1;
                if (instance->definition->fieldCount <= 8) {
                    for (int i = 0; i < instance->definition->fieldCount; i++) {
                        if (instance->definition->fieldNames[i] == name) {
                            fieldIndex = i;
                            break;
                        }
                    }
                } else {
                    Value indexVal;
                    if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                        fieldIndex = AS_INT(indexVal);
                    }
                }
                if (fieldIndex != -1) {
                    pop();
                    push(instance->fields[fieldIndex]);
                    break;
                }
                Value method;
                if (tableGet(&instance->definition->methods, name, &method)) {
                    pop();
                    ObjBoundMethod* bound = newBoundMethod(receiver, AS_CLOSURE(method));
                    push(OBJ_VAL(bound));
                    break;
                }
                runtimeError("Undefined property '%s'.", name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }

            case OP_SET_FIELD: {
                if (!IS_STRUCT(peek(1))) {
                    runtimeError("Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjStruct* instance = AS_STRUCT(peek(1));
                ObjString* name = READ_STRING();
                Value value = pop();

                int fieldIndex = -1;
                // Linear search for small structs (faster), hash table for large
                if (instance->definition->fieldCount <= 8) {
                    for (int i = 0; i < instance->definition->fieldCount; i++) {
                        if (instance->definition->fieldNames[i] == name) {
                            fieldIndex = i;
                            break;
                        }
                    }
                } else {
                    Value indexVal;
                    if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                        fieldIndex = AS_INT(indexVal);
                    }
                }
                if (fieldIndex == -1) {
                    runtimeError("Undefined field '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                instance->fields[fieldIndex] = value;
                pop(); // pop instance
                push(value);
                break;
            }

            case OP_SET_FIELD_LONG: {
                if (!IS_STRUCT(peek(1))) {
                    runtimeError("Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjStruct* instance = AS_STRUCT(peek(1));
                ObjString* name = READ_STRING_LONG();
                Value value = pop();
                int fieldIndex = -1;
                if (instance->definition->fieldCount <= 8) {
                    for (int i = 0; i < instance->definition->fieldCount; i++) {
                        if (instance->definition->fieldNames[i] == name) {
                            fieldIndex = i;
                            break;
                        }
                    }
                } else {
                    Value indexVal;
                    if (tableGet(&instance->definition->fieldIndices, name, &indexVal)) {
                        fieldIndex = AS_INT(indexVal);
                    }
                }
                if (fieldIndex == -1) {
                    runtimeError("Undefined field '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                instance->fields[fieldIndex] = value;
                pop();
                push(value);
                break;
            }

            case OP_METHOD: {
                ObjString* name = READ_STRING();
                Value method = peek(0);
                ObjStructDef* def = AS_STRUCT_DEF(peek(1));
                tableSet(&def->methods, name, method);
                pop(); // pop the closure
                break;
            }

            case OP_INVOKE: {
                ObjString* methodName = READ_STRING();
                int argCount = READ_BYTE();

                Value receiver = peek(argCount);
                if (!IS_STRUCT(receiver)) {
                    runtimeError("Only struct instances have methods.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjStruct* instance = AS_STRUCT(receiver);

                // Look up method
                Value method;
                if (!tableGet(&instance->definition->methods, methodName, &method)) {
                    runtimeError("Undefined method '%s'.", methodName->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Call method with receiver in slot 0
                ObjClosure* closure = AS_CLOSURE(method);
                if (argCount != closure->function->arity) {
                    runtimeError("Expected %d arguments but got %d.",
                        closure->function->arity, argCount);
                    return INTERPRET_RUNTIME_ERROR;
                }

                if (vm.frameCount == FRAMES_MAX) {
                    runtimeError("Stack overflow.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                CallFrame* newFrame = &vm.frames[vm.frameCount++];
                newFrame->closure = closure;
                newFrame->ip = closure->function->chunk->code;
                newFrame->slots = vm.stackTop - argCount - 1;
                frame = newFrame;
                break;
            }

            case OP_IMPORT: {
                ObjString* path = READ_STRING();

                // Read the module file
                char* source = readFile(path->chars);
                if (source == NULL) {
                    runtimeError("Could not open module '%s'.", path->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Compile the module
                ObjFunction* moduleFunc = compile(source);
                free(source);

                if (moduleFunc == NULL) {
                    runtimeError("Error compiling module '%s'.", path->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }

                // Execute the module - it shares globals with the main script
                push(OBJ_VAL(moduleFunc));
                ObjClosure* moduleClosure = newClosure(moduleFunc);
                pop();
                push(OBJ_VAL(moduleClosure));

                CallFrame* moduleFrame = &vm.frames[vm.frameCount++];
                moduleFrame->closure = moduleClosure;
                moduleFrame->ip = moduleFunc->chunk->code;
                moduleFrame->slots = vm.stackTop - 1;

                // Continue execution in the module
                frame = moduleFrame;
                break;
            }

            case OP_RETURN: {
                Value result = pop();
                vm.frameCount--;
                if (vm.frameCount == 0) {
                    pop();
                    return INTERPRET_OK;
                }

                vm.stackTop = frame->slots;
                push(result);
                frame = &vm.frames[vm.frameCount - 1];
                break;
            }

            // === Superinstructions ===
            case OP_GET_LOCAL_0: push(frame->slots[0]); break;
            case OP_GET_LOCAL_1: push(frame->slots[1]); break;
            case OP_GET_LOCAL_2: push(frame->slots[2]); break;
            case OP_GET_LOCAL_3: push(frame->slots[3]); break;

            case OP_INC_LOCAL: {
                uint8_t slot = READ_BYTE();
                Value val = frame->slots[slot];
                if (IS_INT(val)) {
                    frame->slots[slot] = INT_VAL(AS_INT(val) + 1);
                } else if (IS_FLOAT(val)) {
                    frame->slots[slot] = FLOAT_VAL(AS_FLOAT(val) + 1.0);
                } else {
                    runtimeError("Operand must be a number.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_ADD_LOCAL_CONST: {
                uint8_t slot = READ_BYTE();
                Value constant = READ_CONSTANT();
                Value local = frame->slots[slot];
                if (IS_INT(local) && IS_INT(constant)) {
                    push(INT_VAL(AS_INT(local) + AS_INT(constant)));
                } else if (IS_NUMBER(local) && IS_NUMBER(constant)) {
                    push(FLOAT_VAL(AS_NUMBER(local) + AS_NUMBER(constant)));
                } else {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_LESS_LOCAL_CONST: {
                uint8_t slot = READ_BYTE();
                Value constant = READ_CONSTANT();
                Value local = frame->slots[slot];
                if (IS_NUMBER(local) && IS_NUMBER(constant)) {
                    push(BOOL_VAL(AS_NUMBER(local) < AS_NUMBER(constant)));
                } else {
                    runtimeError("Operands must be numbers.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_INDEX_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                Value arrayVal = pop();
                if (!IS_ARRAY(arrayVal)) {
                    runtimeError("Can only index arrays.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                Value indexVal = frame->slots[slot];
                if (!IS_INT(indexVal)) {
                    runtimeError("Array index must be an integer.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                ObjArray* array = AS_ARRAY(arrayVal);
                int64_t index = AS_INT(indexVal);
                if (index < 0 || index >= array->count) {
                    runtimeError("Array index %lld out of bounds [0, %d).",
                                 (long long)index, array->count);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(array->elements[index]);
                break;
            }

            default:
                runtimeError("Unknown opcode %d", instruction);
                return INTERPRET_RUNTIME_ERROR;
        }
    }

#endif // COMPUTED_GOTO

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP_INT
#undef BINARY_OP_FLOAT
#undef BINARY_OP_NUMERIC
}

InterpretResult interpret(const char* source) {
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;

    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = function->chunk->code;
    frame->slots = vm.stack;

    return run();
}
