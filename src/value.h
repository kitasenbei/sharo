#ifndef sharo_value_h
#define sharo_value_h

#include "common.h"

// Forward declaration for heap objects
typedef struct Obj Obj;
typedef struct ObjString ObjString;

// Value types in Sharo
typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_INT,
    VAL_FLOAT,
    VAL_PTR,
    VAL_OBJ,
} ValueType;

// Tagged union for values
typedef struct {
    ValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        void* pointer;
        Obj* obj;
    } as;
} Value;

// Type checking macros
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_INT(value)     ((value).type == VAL_INT)
#define IS_FLOAT(value)   ((value).type == VAL_FLOAT)
#define IS_PTR(value)     ((value).type == VAL_PTR)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

// Check if value is a number (int or float)
#define IS_NUMBER(value)  (IS_INT(value) || IS_FLOAT(value))

// Value extraction macros
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_INT(value)     ((value).as.integer)
#define AS_FLOAT(value)   ((value).as.floating)
#define AS_PTR(value)     ((value).as.pointer)
#define AS_OBJ(value)     ((value).as.obj)

// Value creation macros
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.integer = 0}})
#define INT_VAL(value)    ((Value){VAL_INT, {.integer = value}})
#define FLOAT_VAL(value)  ((Value){VAL_FLOAT, {.floating = value}})
#define PTR_VAL(value)    ((Value){VAL_PTR, {.pointer = value}})
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})

// Convert int to float or vice versa for mixed arithmetic
static inline double AS_NUMBER(Value value) {
    if (IS_INT(value)) return (double)AS_INT(value);
    return AS_FLOAT(value);
}

// Dynamic array of values (for constant pool)
typedef struct {
    int capacity;
    int count;
    Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);

void printValue(Value value);
bool valuesEqual(Value a, Value b);

// Truthiness: false and nil are falsey, everything else is truthy
static inline bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

#endif
