#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "chunk.h"
#include "table.h"
#include "vm.h"

// Allocate an object of given type and size
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->isMarked = false;

    // Add to VM's object list for GC
    object->next = vm.objects;
    vm.objects = object;

    return object;
}

ObjFunction* newFunction(void) {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    function->chunk = ALLOCATE(Chunk, 1);
    initChunk(function->chunk);
    return function;
}

ObjNative* newNative(NativeFn function) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

ObjClosure* newClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

ObjArray* newArray(void) {
    ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    array->count = 0;
    array->capacity = 0;
    array->elements = NULL;
    return array;
}

void writeArray(ObjArray* array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->elements = GROW_ARRAY(Value, array->elements,
                                      oldCapacity, array->capacity);
    }
    array->elements[array->count] = value;
    array->count++;
}

ObjStructDef* newStructDef(ObjString* name) {
    ObjStructDef* def = ALLOCATE_OBJ(ObjStructDef, OBJ_STRUCT_DEF);
    def->name = name;
    def->fieldCount = 0;
    def->fieldNames = NULL;
    initTable(&def->methods);
    return def;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjStruct* newStruct(ObjStructDef* definition) {
    ObjStruct* instance = ALLOCATE_OBJ(ObjStruct, OBJ_STRUCT);
    instance->definition = definition;
    instance->fields = ALLOCATE(Value, definition->fieldCount);
    // Initialize all fields to nil
    for (int i = 0; i < definition->fieldCount; i++) {
        instance->fields[i] = NIL_VAL;
    }
    return instance;
}

// FNV-1a hash function
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;

    // Intern the string
    tableSet(&vm.strings, string, NIL_VAL);

    return string;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // Check if already interned
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    // Check if already interned
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;

    // Allocate and copy
    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';

    return allocateString(heapChars, length, hash);
}

static void printFunction(ObjFunction* function) {
    if (function->name == NULL) {
        printf("<script>");
        return;
    }
    printf("<fn %s>", function->name->chars);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
        case OBJ_FUNCTION:
            printFunction(AS_FUNCTION(value));
            break;
        case OBJ_NATIVE:
            printf("<native fn>");
            break;
        case OBJ_CLOSURE:
            printFunction(AS_CLOSURE(value)->function);
            break;
        case OBJ_UPVALUE:
            printf("<upvalue>");
            break;
        case OBJ_ARRAY: {
            ObjArray* array = AS_ARRAY(value);
            printf("[");
            for (int i = 0; i < array->count; i++) {
                if (i > 0) printf(", ");
                printValue(array->elements[i]);
            }
            printf("]");
            break;
        }
        case OBJ_STRUCT_DEF: {
            ObjStructDef* def = AS_STRUCT_DEF(value);
            printf("<type %s>", def->name->chars);
            break;
        }
        case OBJ_STRUCT: {
            ObjStruct* instance = AS_STRUCT(value);
            printf("%s(", instance->definition->name->chars);
            for (int i = 0; i < instance->definition->fieldCount; i++) {
                if (i > 0) printf(", ");
                printf("%s: ", instance->definition->fieldNames[i]->chars);
                printValue(instance->fields[i]);
            }
            printf(")");
            break;
        }
        case OBJ_BOUND_METHOD:
            printFunction(AS_BOUND_METHOD(value)->method->function);
            break;
    }
}

void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* string = (ObjString*)object;
            FREE_ARRAY(char, string->chars, string->length + 1);
            FREE(ObjString, object);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* function = (ObjFunction*)object;
            freeChunk(function->chunk);
            FREE(Chunk, function->chunk);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE:
            FREE(ObjNative, object);
            break;
        case OBJ_CLOSURE: {
            ObjClosure* closure = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE:
            FREE(ObjUpvalue, object);
            break;
        case OBJ_ARRAY: {
            ObjArray* array = (ObjArray*)object;
            FREE_ARRAY(Value, array->elements, array->capacity);
            FREE(ObjArray, object);
            break;
        }
        case OBJ_STRUCT_DEF: {
            ObjStructDef* def = (ObjStructDef*)object;
            FREE_ARRAY(ObjString*, def->fieldNames, def->fieldCount);
            freeTable(&def->methods);
            FREE(ObjStructDef, object);
            break;
        }
        case OBJ_STRUCT: {
            ObjStruct* instance = (ObjStruct*)object;
            FREE_ARRAY(Value, instance->fields, instance->definition->fieldCount);
            FREE(ObjStruct, object);
            break;
        }
        case OBJ_BOUND_METHOD:
            FREE(ObjBoundMethod, object);
            break;
    }
}
