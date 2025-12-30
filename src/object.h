#ifndef sharo_object_h
#define sharo_object_h

#include "common.h"
#include "value.h"
#include "table.h"

// Forward declare Chunk to avoid circular dependency
typedef struct Chunk Chunk;

// Object types
typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_ARRAY,
    OBJ_STRUCT_DEF,     // Struct type definition
    OBJ_STRUCT,         // Struct instance
    OBJ_BOUND_METHOD,   // Bound method (closure + receiver)
} ObjType;

// Base object structure (header for all heap objects)
struct Obj {
    ObjType type;
    bool isMarked;      // For GC
    struct Obj* next;   // Intrusive linked list for GC
};

// Function object
typedef struct ObjFunction {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk* chunk;       // Pointer to avoid needing full Chunk definition
    ObjString* name;
} ObjFunction;

// Native function type
typedef Value (*NativeFn)(int argCount, Value* args);

// Native function object
typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

// Upvalue object (for closures)
typedef struct ObjUpvalue {
    Obj obj;
    Value* location;
    Value closed;
    struct ObjUpvalue* next;
} ObjUpvalue;

// Closure object
typedef struct ObjClosure {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;
    int upvalueCount;
} ObjClosure;

// String object
struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;      // Cached hash for interning
};

// Array object
typedef struct {
    Obj obj;
    int count;
    int capacity;
    Value* elements;
} ObjArray;

// Struct definition (the "type Point { x: int, y: int }" part)
typedef struct {
    Obj obj;
    ObjString* name;
    int fieldCount;
    ObjString** fieldNames;     // Ordered field names for constructor
    Table methods;              // Methods table
} ObjStructDef;

// Struct instance
typedef struct {
    Obj obj;
    ObjStructDef* definition;
    Value* fields;              // Field values in same order as fieldNames
} ObjStruct;

// Bound method (method + receiver instance)
typedef struct {
    Obj obj;
    Value receiver;             // The struct instance
    ObjClosure* method;         // The method closure
} ObjBoundMethod;

// Object type checking
#define OBJ_TYPE(value)     (AS_OBJ(value)->type)

#define IS_STRING(value)    isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)
#define IS_ARRAY(value)     isObjType(value, OBJ_ARRAY)
#define IS_STRUCT_DEF(value) isObjType(value, OBJ_STRUCT_DEF)
#define IS_STRUCT(value)    isObjType(value, OBJ_STRUCT)
#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)

// Object casting
#define AS_STRING(value)    ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)   (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value)  ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)    (((ObjNative*)AS_OBJ(value))->function)
#define AS_CLOSURE(value)   ((ObjClosure*)AS_OBJ(value))
#define AS_ARRAY(value)     ((ObjArray*)AS_OBJ(value))
#define AS_STRUCT_DEF(value) ((ObjStructDef*)AS_OBJ(value))
#define AS_STRUCT(value)    ((ObjStruct*)AS_OBJ(value))
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// Object creation
ObjFunction* newFunction(void);
ObjNative* newNative(NativeFn function);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjArray* newArray(void);
void writeArray(ObjArray* array, Value value);
ObjStructDef* newStructDef(ObjString* name);
ObjStruct* newStruct(ObjStructDef* definition);
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);

void printObject(Value value);
void freeObject(Obj* object);

#endif
