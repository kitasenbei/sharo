#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "object.h"
#include "scanner.h"
#include "vm.h"

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

// Test: run hand-written bytecode
static void testVM(void) {
    printf("=== Testing VM with hand-written bytecode ===\n\n");

    // Create a function to hold our bytecode
    ObjFunction* function = newFunction();
    Chunk* chunk = function->chunk;

    // Test 1: Integer arithmetic: (10 + 5) * 3 - 2 = 43
    printf("Test 1: (10 + 5) * 3 - 2\n");

    int constant = addConstant(chunk, INT_VAL(10));
    writeChunk(chunk, OP_CONSTANT, 1);
    writeChunk(chunk, constant, 1);

    constant = addConstant(chunk, INT_VAL(5));
    writeChunk(chunk, OP_CONSTANT, 1);
    writeChunk(chunk, constant, 1);

    writeChunk(chunk, OP_ADD, 1);

    constant = addConstant(chunk, INT_VAL(3));
    writeChunk(chunk, OP_CONSTANT, 1);
    writeChunk(chunk, constant, 1);

    writeChunk(chunk, OP_MULTIPLY, 1);

    constant = addConstant(chunk, INT_VAL(2));
    writeChunk(chunk, OP_CONSTANT, 1);
    writeChunk(chunk, constant, 1);

    writeChunk(chunk, OP_SUBTRACT, 1);

    writeChunk(chunk, OP_PRINT, 1);

    // Test 2: Float arithmetic: 3.14 * 2.0 = 6.28
    printf("Test 2: 3.14 * 2.0\n");

    constant = addConstant(chunk, FLOAT_VAL(3.14));
    writeChunk(chunk, OP_CONSTANT, 2);
    writeChunk(chunk, constant, 2);

    constant = addConstant(chunk, FLOAT_VAL(2.0));
    writeChunk(chunk, OP_CONSTANT, 2);
    writeChunk(chunk, constant, 2);

    writeChunk(chunk, OP_MULTIPLY, 2);
    writeChunk(chunk, OP_PRINT, 2);

    // Test 3: Boolean and nil
    printf("Test 3: true, false, nil\n");
    writeChunk(chunk, OP_TRUE, 3);
    writeChunk(chunk, OP_PRINT, 3);
    writeChunk(chunk, OP_FALSE, 3);
    writeChunk(chunk, OP_PRINT, 3);
    writeChunk(chunk, OP_NIL, 3);
    writeChunk(chunk, OP_PRINT, 3);

    // Test 4: Comparison: 10 > 5
    printf("Test 4: 10 > 5\n");
    constant = addConstant(chunk, INT_VAL(10));
    writeChunk(chunk, OP_CONSTANT, 4);
    writeChunk(chunk, constant, 4);

    constant = addConstant(chunk, INT_VAL(5));
    writeChunk(chunk, OP_CONSTANT, 4);
    writeChunk(chunk, constant, 4);

    writeChunk(chunk, OP_GREATER, 4);
    writeChunk(chunk, OP_PRINT, 4);

    // Test 5: Negation: -42
    printf("Test 5: -42\n");
    constant = addConstant(chunk, INT_VAL(42));
    writeChunk(chunk, OP_CONSTANT, 5);
    writeChunk(chunk, constant, 5);
    writeChunk(chunk, OP_NEGATE, 5);
    writeChunk(chunk, OP_PRINT, 5);

    // Test 6: Logical not: !false = true
    printf("Test 6: !false\n");
    writeChunk(chunk, OP_FALSE, 6);
    writeChunk(chunk, OP_NOT, 6);
    writeChunk(chunk, OP_PRINT, 6);

    // Test 7: String
    printf("Test 7: \"Hello, Sharo!\"\n");
    ObjString* str = copyString("Hello, Sharo!", 13);
    constant = addConstant(chunk, OBJ_VAL(str));
    writeChunk(chunk, OP_CONSTANT, 7);
    writeChunk(chunk, constant, 7);
    writeChunk(chunk, OP_PRINT, 7);

    // Test 8: String concatenation
    printf("Test 8: \"Hello, \" + \"World!\"\n");
    ObjString* str1 = copyString("Hello, ", 7);
    constant = addConstant(chunk, OBJ_VAL(str1));
    writeChunk(chunk, OP_CONSTANT, 8);
    writeChunk(chunk, constant, 8);

    ObjString* str2 = copyString("World!", 6);
    constant = addConstant(chunk, OBJ_VAL(str2));
    writeChunk(chunk, OP_CONSTANT, 8);
    writeChunk(chunk, constant, 8);

    writeChunk(chunk, OP_ADD, 8);
    writeChunk(chunk, OP_PRINT, 8);

    // Test 9: Modulo: 17 % 5 = 2
    printf("Test 9: 17 %% 5\n");
    constant = addConstant(chunk, INT_VAL(17));
    writeChunk(chunk, OP_CONSTANT, 9);
    writeChunk(chunk, constant, 9);

    constant = addConstant(chunk, INT_VAL(5));
    writeChunk(chunk, OP_CONSTANT, 9);
    writeChunk(chunk, constant, 9);

    writeChunk(chunk, OP_MODULO, 9);
    writeChunk(chunk, OP_PRINT, 9);

    // Return
    writeChunk(chunk, OP_NIL, 10);
    writeChunk(chunk, OP_RETURN, 10);

    // Disassemble
    printf("\n=== Bytecode Disassembly ===\n");
    disassembleChunk(chunk, "test");

    // Run
    printf("\n=== Execution Output ===\n");
    ObjClosure* closure = newClosure(function);
    push(OBJ_VAL(closure));

    CallFrame* frame = &vm.frames[vm.frameCount++];
    frame->closure = closure;
    frame->ip = function->chunk->code;
    frame->slots = vm.stack;

    InterpretResult result = INTERPRET_OK;

    // Run the VM loop manually (since interpret() isn't hooked up yet)
    #define READ_BYTE() (*frame->ip++)
    #define READ_CONSTANT() (frame->closure->function->chunk->constants.values[READ_BYTE()])

    for (;;) {
        uint8_t instruction = READ_BYTE();
        switch (instruction) {
            case OP_CONSTANT: push(READ_CONSTANT()); break;
            case OP_NIL: push(NIL_VAL); break;
            case OP_TRUE: push(BOOL_VAL(true)); break;
            case OP_FALSE: push(BOOL_VAL(false)); break;
            case OP_ADD: {
                if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
                    ObjString* b = AS_STRING(pop());
                    ObjString* a = AS_STRING(pop());
                    int length = a->length + b->length;
                    char* chars = (char*)malloc(length + 1);
                    memcpy(chars, a->chars, a->length);
                    memcpy(chars + a->length, b->chars, b->length);
                    chars[length] = '\0';
                    ObjString* res = takeString(chars, length);
                    push(OBJ_VAL(res));
                } else if (IS_INT(peek(0)) && IS_INT(peek(1))) {
                    int64_t b = AS_INT(pop());
                    int64_t a = AS_INT(pop());
                    push(INT_VAL(a + b));
                } else {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(FLOAT_VAL(a + b));
                }
                break;
            }
            case OP_SUBTRACT: {
                if (IS_INT(peek(0)) && IS_INT(peek(1))) {
                    int64_t b = AS_INT(pop());
                    int64_t a = AS_INT(pop());
                    push(INT_VAL(a - b));
                } else {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(FLOAT_VAL(a - b));
                }
                break;
            }
            case OP_MULTIPLY: {
                if (IS_INT(peek(0)) && IS_INT(peek(1))) {
                    int64_t b = AS_INT(pop());
                    int64_t a = AS_INT(pop());
                    push(INT_VAL(a * b));
                } else {
                    double b = AS_NUMBER(pop());
                    double a = AS_NUMBER(pop());
                    push(FLOAT_VAL(a * b));
                }
                break;
            }
            case OP_MODULO: {
                int64_t b = AS_INT(pop());
                int64_t a = AS_INT(pop());
                push(INT_VAL(a % b));
                break;
            }
            case OP_GREATER: {
                double b = AS_NUMBER(pop());
                double a = AS_NUMBER(pop());
                push(BOOL_VAL(a > b));
                break;
            }
            case OP_NEGATE: {
                if (IS_INT(peek(0))) {
                    push(INT_VAL(-AS_INT(pop())));
                } else {
                    push(FLOAT_VAL(-AS_FLOAT(pop())));
                }
                break;
            }
            case OP_NOT: push(BOOL_VAL(isFalsey(pop()))); break;
            case OP_PRINT: printValue(pop()); printf("\n"); break;
            case OP_RETURN:
                result = INTERPRET_OK;
                goto done;
        }
    }
done:

    #undef READ_BYTE
    #undef READ_CONSTANT

    printf("\n=== VM Test Complete ===\n");
    printf("Result: %s\n", result == INTERPRET_OK ? "OK" : "ERROR");
}

static void runScanner(const char* source) {
    initScanner(source);
    int line = -1;

    for (;;) {
        Token token = scanToken();

        if (token.line != line) {
            printf("%4d ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }

        printf("%-16s '%.*s'\n", tokenTypeName(token.type), token.length, token.start);

        if (token.type == TOKEN_EOF) break;
    }
}

static void repl(void) {
    char line[1024];
    printf("Sharo REPL (type 'exit' to quit, 'test' to run VM test, 'scan' to toggle scanner mode)\n");
    bool scannerMode = false;

    for (;;) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        if (strcmp(line, "exit\n") == 0) break;
        if (strcmp(line, "test\n") == 0) {
            testVM();
            continue;
        }
        if (strcmp(line, "scan\n") == 0) {
            scannerMode = !scannerMode;
            printf("Scanner mode: %s\n", scannerMode ? "ON" : "OFF");
            continue;
        }

        if (scannerMode) {
            runScanner(line);
        } else {
            interpret(line);
        }
    }
}

static void runFile(const char* path) {
    char* source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, char* argv[]) {
    initVM();

    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        if (strcmp(argv[1], "--test") == 0) {
            testVM();
        } else {
            runFile(argv[1]);
        }
    } else {
        fprintf(stderr, "Usage: sharo [path] | --test\n");
        exit(64);
    }

    freeVM();
    return 0;
}
