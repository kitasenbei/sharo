#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

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

            default:
                runtimeError("Unknown opcode %d", instruction);
                return INTERPRET_RUNTIME_ERROR;
        }
    }

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
