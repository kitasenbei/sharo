#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "object.h"

void initValueArray(ValueArray* array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(Value, array->values,
                                   oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(ValueArray* array) {
    FREE_ARRAY(Value, array->values, array->capacity);
    initValueArray(array);
}

void printValue(Value value) {
#ifdef NAN_BOXING
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_INT(value)) {
        printf("%ld", AS_INT(value));
    } else if (IS_FLOAT(value)) {
        printf("%g", AS_FLOAT(value));
    } else if (IS_PTR(value)) {
        printf("<ptr %p>", AS_PTR(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    }
#else
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_INT:
            printf("%ld", AS_INT(value));
            break;
        case VAL_FLOAT:
            printf("%g", AS_FLOAT(value));
            break;
        case VAL_PTR:
            printf("<ptr %p>", AS_PTR(value));
            break;
        case VAL_OBJ:
            printObject(value);
            break;
    }
#endif
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
    // With NaN boxing, most equality is just bit comparison
    // But we need special handling for int/float comparison
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);
    }
    // For everything else (bool, nil, obj, ptr), bit equality works
    return a == b;
#else
    // Different types are never equal (except int/float comparison)
    if (a.type != b.type) {
        // Allow int == float comparison
        if (IS_NUMBER(a) && IS_NUMBER(b)) {
            return AS_NUMBER(a) == AS_NUMBER(b);
        }
        return false;
    }

    switch (a.type) {
        case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:    return true;
        case VAL_INT:    return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT:  return AS_FLOAT(a) == AS_FLOAT(b);
        case VAL_PTR:    return AS_PTR(a) == AS_PTR(b);
        case VAL_OBJ:    return AS_OBJ(a) == AS_OBJ(b); // Pointer comparison (interned strings)
        default:         return false;
    }
#endif
}
