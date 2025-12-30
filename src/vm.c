#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// TCP sockets (POSIX)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

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

    SDL_Surface* surface = SDL_LoadBMP(path);
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

// ============ String Native Functions ============

// chr(code) -> single character string
static Value chrNative(int argCount, Value* args) {
    (void)argCount;
    int code = (int)AS_INT(args[0]);
    char c = (char)code;
    return OBJ_VAL(copyString(&c, 1));
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

    // SDL3 functions
    defineNative("init", initNative);
    defineNative("quit", quitNative);
    defineNative("createWindow", createWindowNative);
    defineNative("destroyWindow", destroyWindowNative);
    defineNative("createRenderer", createRendererNative);
    defineNative("destroyRenderer", destroyRendererNative);
    defineNative("clear", clearNative);
    defineNative("present", presentNative);
    defineNative("setDrawColor", setDrawColorNative);
    defineNative("fillRect", fillRectNative);
    defineNative("drawRect", drawRectNative);
    defineNative("pollEvent", pollEventNative);
    defineNative("eventKey", eventKeyNative);
    defineNative("delay", delayNative);
    defineNative("getTicks", getTicksNative);
    defineNative("loadTexture", loadTextureNative);
    defineNative("destroyTexture", destroyTextureNative);
    defineNative("drawTexture", drawTextureNative);
    defineNative("getTextureSize", getTextureSizeNative);
    defineNative("random", randomNative);
    defineNative("randomFloat", randomFloatNative);

    // TTF functions
    defineNative("initTTF", initTTFNative);
    defineNative("quitTTF", quitTTFNative);
    defineNative("loadFont", loadFontNative);
    defineNative("destroyFont", destroyFontNative);
    defineNative("drawText", drawTextNative);

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

static InterpretResult run(void) {
    CallFrame* frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure->function->chunk->constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())

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
        &&do_NIL,            // OP_NIL
        &&do_TRUE,           // OP_TRUE
        &&do_FALSE,          // OP_FALSE
        &&do_POP,            // OP_POP
        &&do_DUP,            // OP_DUP
        &&do_GET_LOCAL,      // OP_GET_LOCAL
        &&do_SET_LOCAL,      // OP_SET_LOCAL
        &&do_GET_GLOBAL,     // OP_GET_GLOBAL
        &&do_DEFINE_GLOBAL,  // OP_DEFINE_GLOBAL
        &&do_SET_GLOBAL,     // OP_SET_GLOBAL
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
        &&do_SET_FIELD,      // OP_SET_FIELD
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
    do_NIL:   push(NIL_VAL); DISPATCH();
    do_TRUE:  push(BOOL_VAL(true)); DISPATCH();
    do_FALSE: push(BOOL_VAL(false)); DISPATCH();
    do_POP:   pop(); DISPATCH();
    do_DUP:   push(peek(0)); DISPATCH();

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
    do_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
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
        for (int i = 0; i < instance->definition->fieldCount; i++) {
            if (instance->definition->fieldNames[i] == name) {
                fieldIndex = i;
                break;
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
        for (int i = 0; i < instance->definition->fieldCount; i++) {
            if (instance->definition->fieldNames[i] == name) {
                fieldIndex = i;
                break;
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
            case OP_NIL:   push(NIL_VAL); break;
            case OP_TRUE:  push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;

            case OP_POP: pop(); break;
            case OP_DUP: push(peek(0)); break;

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
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
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

                // Find field index
                int fieldIndex = -1;
                for (int i = 0; i < instance->definition->fieldCount; i++) {
                    if (instance->definition->fieldNames[i] == name) {
                        fieldIndex = i;
                        break;
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

            case OP_SET_FIELD: {
                if (!IS_STRUCT(peek(1))) {
                    runtimeError("Only struct instances have fields.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                ObjStruct* instance = AS_STRUCT(peek(1));
                ObjString* name = READ_STRING();
                Value value = pop();

                // Find field index
                int fieldIndex = -1;
                for (int i = 0; i < instance->definition->fieldCount; i++) {
                    if (instance->definition->fieldNames[i] == name) {
                        fieldIndex = i;
                        break;
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
