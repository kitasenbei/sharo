#ifndef sharo_value_h
#define sharo_value_h

#include <string.h>
#include "common.h"

// Forward declaration for heap objects
typedef struct Obj Obj;
typedef struct ObjString ObjString;

#ifdef NAN_BOXING

// ============================================================
// NaN Boxing: Pack all values into 64 bits
// ============================================================
// Layout:
//   - Regular double: any value where (v & QNAN) != QNAN
//   - qNaN values use the payload for type + data:
//     - Singletons: QNAN | 1=nil, 2=false, 3=true
//     - Int (48-bit): QNAN | TAG_INT | int48
//     - Ptr (48-bit): QNAN | TAG_PTR | ptr48
//     - Obj pointer:  QNAN | SIGN_BIT | ptr48

typedef uint64_t Value;

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7ffc000000000000)

// Tags in bits 49-48 of the qNaN payload
#define TAG_INT  ((uint64_t)0x0001000000000000)
#define TAG_PTR  ((uint64_t)0x0002000000000000)
#define TAG_MASK ((uint64_t)0x0003000000000000)

// Singleton markers (in low bits when no tag set)
#define SINGLETON_NIL   1
#define SINGLETON_FALSE 2
#define SINGLETON_TRUE  3

// Singleton values
#define NIL_VAL   ((Value)(QNAN | SINGLETON_NIL))
#define FALSE_VAL ((Value)(QNAN | SINGLETON_FALSE))
#define TRUE_VAL  ((Value)(QNAN | SINGLETON_TRUE))

// Type checks
#define IS_FLOAT(value) (((value) & QNAN) != QNAN)
#define IS_NIL(value)   ((value) == NIL_VAL)
#define IS_BOOL(value)  (((value) | 1) == TRUE_VAL)
#define IS_OBJ(value)   (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))
#define IS_INT(value)   (!IS_OBJ(value) && (((value) & (QNAN | TAG_MASK)) == (QNAN | TAG_INT)))
#define IS_PTR(value)   (!IS_OBJ(value) && (((value) & (QNAN | TAG_MASK)) == (QNAN | TAG_PTR)))
#define IS_NUMBER(value) (IS_FLOAT(value) || IS_INT(value))

// Value extraction
#define AS_BOOL(value)  ((value) == TRUE_VAL)
#define AS_OBJ(value)   ((Obj*)(uintptr_t)((value) & 0x0000FFFFFFFFFFFF))
#define AS_PTR(value)   ((void*)(uintptr_t)((value) & 0x0000FFFFFFFFFFFF))

// Int extraction with sign extension from 48 bits
static inline int64_t nanbox_as_int(Value value) {
    int64_t i = (int64_t)(value & 0x0000FFFFFFFFFFFF);
    if (i & 0x0000800000000000) i |= (int64_t)0xFFFF000000000000;
    return i;
}
#define AS_INT(value) nanbox_as_int(value)

// Float extraction via type punning
static inline double nanbox_as_float(Value value) {
    double d;
    memcpy(&d, &value, sizeof(Value));
    return d;
}
#define AS_FLOAT(value) nanbox_as_float(value)

// Number extraction (int or float -> double)
static inline double nanbox_as_number(Value value) {
    if (IS_FLOAT(value)) return nanbox_as_float(value);
    return (double)nanbox_as_int(value);
}
#define AS_NUMBER(value) nanbox_as_number(value)

// Value creation
#define BOOL_VAL(b)   ((b) ? TRUE_VAL : FALSE_VAL)
#define OBJ_VAL(obj)  ((Value)(QNAN | SIGN_BIT | ((uint64_t)(uintptr_t)(obj) & 0x0000FFFFFFFFFFFF)))
#define PTR_VAL(ptr)  ((Value)(QNAN | TAG_PTR | ((uint64_t)(uintptr_t)(ptr) & 0x0000FFFFFFFFFFFF)))
#define INT_VAL(i)    ((Value)(QNAN | TAG_INT | ((uint64_t)(i) & 0x0000FFFFFFFFFFFF)))

static inline Value nanbox_float(double d) {
    Value v;
    memcpy(&v, &d, sizeof(double));
    return v;
}
#define FLOAT_VAL(d) nanbox_float(d)

#else // !NAN_BOXING

// ============================================================
// Tagged Union: Traditional 16-byte value representation
// ============================================================

typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_INT,
    VAL_FLOAT,
    VAL_PTR,
    VAL_OBJ,
} ValueType;

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

#endif // NAN_BOXING

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
