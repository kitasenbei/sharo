#ifndef sharo_chunk_h
#define sharo_chunk_h

#include "common.h"
#include "value.h"

// Bytecode opcodes
typedef enum {
    // Constants and literals
    OP_CONSTANT,        // Load constant from pool
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    // Stack operations
    OP_POP,
    OP_DUP,             // Duplicate top of stack

    // Variables
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,

    // Properties (for structs)
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,

    // Comparison
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,

    // Arithmetic (integer)
    OP_ADD_INT,
    OP_SUBTRACT_INT,
    OP_MULTIPLY_INT,
    OP_DIVIDE_INT,
    OP_MODULO_INT,
    OP_NEGATE_INT,

    // Arithmetic (float)
    OP_ADD_FLOAT,
    OP_SUBTRACT_FLOAT,
    OP_MULTIPLY_FLOAT,
    OP_DIVIDE_FLOAT,
    OP_NEGATE_FLOAT,

    // Generic arithmetic (runtime type dispatch)
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_NEGATE,

    // Logical
    OP_NOT,

    // Type conversion
    OP_INT_TO_FLOAT,
    OP_FLOAT_TO_INT,

    // Control flow
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    // Functions
    OP_CALL,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,

    // Native/FFI
    OP_NATIVE_CALL,

    // Built-in operations
    OP_PRINT,

    // Structs
    OP_STRUCT_DEF,      // Define struct type (field count follows)
    OP_STRUCT_FIELD,    // Add field name to struct def (name constant follows)
    OP_STRUCT_CALL,     // Constructor call: create instance with N args
    OP_GET_FIELD,       // Get field by name
    OP_SET_FIELD,       // Set field by name

    // Arrays
    OP_ARRAY,           // Create array from N stack elements
    OP_INDEX_GET,       // arr[index] - get element
    OP_INDEX_SET,       // arr[index] = value - set element

    // Methods
    OP_METHOD,          // Define a method on struct type
    OP_INVOKE,          // Invoke method directly (optimization)
    OP_GET_SELF,        // Get 'self' for method body

    // Modules
    OP_IMPORT,          // Import a module

    // === Superinstructions ===
    // Single-byte local access (no operand - slot encoded in opcode)
    OP_GET_LOCAL_0,
    OP_GET_LOCAL_1,
    OP_GET_LOCAL_2,
    OP_GET_LOCAL_3,

    // Increment local by 1: local = local + 1 (operand: slot)
    OP_INC_LOCAL,

    // Load local, load constant, add: push(local[slot] + const[idx])
    // Operands: slot (1 byte), constant index (1 byte)
    OP_ADD_LOCAL_CONST,

    // Load local, compare to constant: push(local[slot] < const[idx])
    // Operands: slot (1 byte), constant index (1 byte)
    OP_LESS_LOCAL_CONST,

    // Array index with local: push(stack[-1][local[slot]])
    // Operand: slot (1 byte)
    OP_INDEX_GET_LOCAL,
} OpCode;

// Bytecode chunk - use struct tag for forward declaration compatibility
typedef struct Chunk {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;         // Line numbers for debugging
    ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
