#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "chunk.h"
#include "object.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// Parser state
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// Precedence levels (lowest to highest)
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * / %
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

// Local variable
typedef struct {
    Token name;
    int depth;
    bool isCaptured;
} Local;

// Upvalue
typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

// Function type
typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
} FunctionType;

// Track current struct being defined (for methods)
typedef struct TypeCompiler {
    struct TypeCompiler* enclosing;
    ObjStructDef* definition;
} TypeCompiler;

// Compiler state (one per function being compiled)
typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;
TypeCompiler* currentType = NULL;

static Chunk* currentChunk(void) {
    return current->function->chunk;
}

// Error handling
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// Token handling
static void advance(void) {
    parser.previous = parser.current;

    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// Bytecode emission
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void emitReturn(void) {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction();
    current = compiler;

    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start,
                                              parser.previous.length);
    }

    // Slot 0 for internal use - 'self' for methods, empty for functions
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    if (type == TYPE_METHOD) {
        local->name.start = "self";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction* endCompiler(void) {
    emitReturn();
    ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), function->name != NULL
                         ? function->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}

static void beginScope(void) {
    current->scopeDepth++;
}

static void endScope(void) {
    current->scopeDepth--;

    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

// Forward declarations
static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static void funDeclaration(bool parenConsumed);
static void function(FunctionType type, bool parenConsumed);

static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static void declareVariable(void) {
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized(void) {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

// Expression parsing
static void binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    emitByte(OP_NOT_EQUAL); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitByte(OP_LESS_EQUAL); break;
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        case TOKEN_PERCENT:       emitByte(OP_MODULO); break;
        default: return; // Unreachable
    }
}

static uint8_t argumentList(void) {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void call(bool canAssign) {
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL:   emitByte(OP_NIL); break;
        case TOKEN_TRUE:  emitByte(OP_TRUE); break;
        default: return; // Unreachable
    }
}

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
    (void)canAssign;

    const char* start = parser.previous.start;
    int length = parser.previous.length;

    // Check for hex
    if (length > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        int64_t value = strtoll(start, NULL, 16);
        emitConstant(INT_VAL(value));
        return;
    }

    // Check for binary
    if (length > 2 && start[0] == '0' && (start[1] == 'b' || start[1] == 'B')) {
        int64_t value = strtoll(start + 2, NULL, 2);
        emitConstant(INT_VAL(value));
        return;
    }

    // Check if float (has decimal point or exponent)
    bool isFloat = false;
    for (int i = 0; i < length; i++) {
        if (start[i] == '.' || start[i] == 'e' || start[i] == 'E') {
            isFloat = true;
            break;
        }
    }

    if (isFloat || parser.previous.type == TOKEN_NUMBER_FLOAT) {
        double value = strtod(start, NULL);
        emitConstant(FLOAT_VAL(value));
    } else {
        int64_t value = strtoll(start, NULL, 10);
        emitConstant(INT_VAL(value));
    }
}

static void string(bool canAssign) {
    (void)canAssign;
    // Strip the quotes
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                     parser.previous.length - 2)));
}

static void arrayLiteral(bool canAssign) {
    (void)canAssign;
    int elementCount = 0;

    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            expression();
            if (elementCount == 255) {
                error("Can't have more than 255 elements in array literal.");
            }
            elementCount++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
    emitBytes(OP_ARRAY, (uint8_t)elementCount);
}

static void subscript(bool canAssign) {
    // Parse the index expression
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_INDEX_SET);
    } else {
        emitByte(OP_INDEX_GET);
    }
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_FIELD, name);
    } else {
        emitBytes(OP_GET_FIELD, name);
    }
}

static void self_(bool canAssign) {
    (void)canAssign;
    if (currentType == NULL) {
        error("Can't use 'self' outside of a type definition.");
        return;
    }
    // self is always in local slot 0 of methods - use superinstruction
    emitByte(OP_GET_LOCAL_0);
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);

    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else if (canAssign && match(TOKEN_COLON_EQUAL)) {
        // := is only for declaration, not reassignment
        error("Use '=' for assignment, ':=' is for declaration.");
    } else {
        // Superinstruction: single-byte local access for slots 0-3
        if (getOp == OP_GET_LOCAL && arg >= 0 && arg <= 3) {
            emitByte(OP_GET_LOCAL_0 + arg);
        } else {
            emitBytes(getOp, (uint8_t)arg);
        }
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;

    // Compile the operand
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction
    switch (operatorType) {
        case TOKEN_BANG:
        case TOKEN_NOT:
            emitByte(OP_NOT);
            break;
        case TOKEN_MINUS:
            emitByte(OP_NEGATE);
            break;
        default: return; // Unreachable
    }
}

// Parse rules table
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {arrayLiteral, subscript, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     dot,    PREC_CALL},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,    NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary, PREC_COMPARISON},
    [TOKEN_COLON]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COLON_EQUAL]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ARROW]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_QUESTION]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_AT]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_AMPERSAND]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IDENTIFIER]    = {variable, NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,   NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_NUMBER_FLOAT]  = {number,   NULL,   PREC_NONE},
    [TOKEN_INT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FLOAT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_BOOL]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_STR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PTR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_BYTE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_VOID]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,  NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,  NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,  NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IN]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MATCH]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_BREAK]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CONTINUE]      = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TYPE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EXTERN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SELF]          = {self_,    NULL,   PREC_NONE},
    [TOKEN_IMPORT]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EXPORT]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_OR]            = {NULL,     or_,    PREC_OR},
    [TOKEN_NOT]           = {unary,    NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

static void expression(void) {
    parsePrecedence(PREC_ASSIGNMENT);
}

// Statements
static void block(void) {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void expressionStatement(void) {
    expression();
    emitByte(OP_POP);
}

static void ifStatement(void) {
    expression();

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    consume(TOKEN_LEFT_BRACE, "Expect '{' after if condition.");
    beginScope();
    block();
    endScope();

    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) {
        if (match(TOKEN_IF)) {
            ifStatement();
        } else {
            consume(TOKEN_LEFT_BRACE, "Expect '{' after else.");
            beginScope();
            block();
            endScope();
        }
    }

    patchJump(elseJump);
}

static void printStatement(void) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'print'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after value.");
    emitByte(OP_PRINT);
}

static void returnStatement(void) {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (check(TOKEN_RIGHT_BRACE) || check(TOKEN_EOF)) {
        emitReturn();
    } else {
        expression();
        emitByte(OP_RETURN);
    }
}

static void forStatement(void) {
    beginScope();
    int loopStart = currentChunk()->count;

    if (check(TOKEN_LEFT_BRACE)) {
        // Infinite loop: for { }
        consume(TOKEN_LEFT_BRACE, "Expect '{'.");
        beginScope();
        block();
        endScope();
        emitLoop(loopStart);
    } else {
        // for condition { body }
        expression();
        int exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);

        consume(TOKEN_LEFT_BRACE, "Expect '{' after for condition.");
        beginScope();
        block();
        endScope();

        emitLoop(loopStart);
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    endScope();
}

// Forward declare for method compilation
static void function(FunctionType type, bool parenConsumed);

static void method(Token methodName) {
    // methodName(params...) returnType { body }
    // Note: methodName token already consumed by caller
    uint8_t constant = identifierConstant(&methodName);

    // Compile the method as a function
    FunctionType type = TYPE_METHOD;

    // Set previous to method name so initCompiler can use it
    parser.previous = methodName;

    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after method name.");

    // Parameters
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t paramConstant = parseVariable("Expect parameter name.");
            (void)paramConstant;

            // Skip type annotation
            if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
                check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_IDENTIFIER)) {
                advance();
            }

            markInitialized();
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    // Skip return type annotation
    if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
        check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_VOID) ||
        check(TOKEN_IDENTIFIER)) {
        advance();
    }

    // Body
    consume(TOKEN_LEFT_BRACE, "Expect '{' before method body.");
    block();

    ObjFunction* fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(fn)));

    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }

    emitBytes(OP_METHOD, constant);
}

static void typeDeclaration(void) {
    // type Point { x: int, y: int methodName() { ... } }
    consume(TOKEN_IDENTIFIER, "Expect type name.");
    Token typeName = parser.previous;
    uint8_t nameConstant = identifierConstant(&typeName);

    consume(TOKEN_LEFT_BRACE, "Expect '{' after type name.");

    // Count fields first (fields are name: type, methods are name(...))
    int fieldCount = 0;
    ObjString* fieldNames[256];

    // Set up type compiler for method compilation (needed before parsing)
    TypeCompiler typeCompiler;
    typeCompiler.enclosing = currentType;
    currentType = &typeCompiler;

    // Parse fields and methods
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        if (!check(TOKEN_IDENTIFIER)) break;

        // Peek ahead to see if this is a method (has '(') or field (has ':')
        Token name = parser.current;
        advance(); // consume identifier

        if (check(TOKEN_LEFT_PAREN)) {
            // This is a method - emit struct def and fields first if not done
            if (fieldCount >= 0) {
                // Emit: create struct def with name constant, then add fields
                emitBytes(OP_STRUCT_DEF, (uint8_t)fieldCount);
                emitByte(nameConstant);

                // Emit field names
                for (int i = 0; i < fieldCount; i++) {
                    uint8_t fieldNameConstant = makeConstant(OBJ_VAL(fieldNames[i]));
                    emitBytes(OP_STRUCT_FIELD, fieldNameConstant);
                }

                // Mark that we've emitted the struct def
                fieldCount = -1;
            }

            // Now compile the method
            method(name);
            continue;
        }

        // This is a field
        fieldNames[fieldCount] = copyString(name.start, name.length);

        consume(TOKEN_COLON, "Expect ':' after field name.");

        // Skip type annotation (for now, not enforced)
        if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
            check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_IDENTIFIER)) {
            advance();
        }

        fieldCount++;
        if (fieldCount > 255) {
            error("Can't have more than 255 fields in a struct.");
        }

        // Optional comma
        match(TOKEN_COMMA);
    }

    // If we haven't emitted the struct def yet (no methods), do it now
    if (fieldCount >= 0) {
        // Emit: create struct def with name constant, then add fields
        emitBytes(OP_STRUCT_DEF, (uint8_t)fieldCount);
        emitByte(nameConstant);

        // Emit field names
        for (int i = 0; i < fieldCount; i++) {
            uint8_t fieldNameConstant = makeConstant(OBJ_VAL(fieldNames[i]));
            emitBytes(OP_STRUCT_FIELD, fieldNameConstant);
        }
    }

    currentType = currentType->enclosing;

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after type body.");

    // Define as global variable
    if (current->scopeDepth > 0) {
        addLocal(typeName);
        markInitialized();
    } else {
        emitBytes(OP_DEFINE_GLOBAL, nameConstant);
    }
}

static void importStatement(void) {
    // import "path/to/module.sharo"
    consume(TOKEN_STRING, "Expect module path after 'import'.");
    uint8_t pathConstant = makeConstant(OBJ_VAL(copyString(
        parser.previous.start + 1,
        parser.previous.length - 2)));
    emitBytes(OP_IMPORT, pathConstant);
}

static void statement(void) {
    if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_TYPE)) {
        typeDeclaration();
    } else if (match(TOKEN_IMPORT)) {
        importStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else if (match(TOKEN_IDENTIFIER)) {
        // Check if it's print() call or function call or variable decl
        Token name = parser.previous;

        if (name.length == 5 && memcmp(name.start, "print", 5) == 0) {
            printStatement();
        } else if (check(TOKEN_COLON_EQUAL)) {
            // Variable declaration: x := expr
            advance(); // consume :=
            expression();

            if (current->scopeDepth > 0) {
                addLocal(name);
                markInitialized();
            } else {
                uint8_t global = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
                emitBytes(OP_DEFINE_GLOBAL, global);
            }
        } else if (check(TOKEN_COLON)) {
            // Constant or typed declaration: x : type = expr OR x : value
            advance(); // consume :

            // Skip type annotation if present
            if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
                check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_IDENTIFIER)) {
                advance(); // skip type
            }

            if (match(TOKEN_COLON)) {
                // Constant: x : type : value OR x : value (shorthand)
                expression();

                if (current->scopeDepth > 0) {
                    addLocal(name);
                    markInitialized();
                } else {
                    uint8_t global = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
                    emitBytes(OP_DEFINE_GLOBAL, global);
                }
            } else if (match(TOKEN_EQUAL)) {
                // Typed mutable: x : type = value
                expression();

                if (current->scopeDepth > 0) {
                    addLocal(name);
                    markInitialized();
                } else {
                    uint8_t global = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
                    emitBytes(OP_DEFINE_GLOBAL, global);
                }
            } else {
                error("Expect '=' or ':' after type annotation.");
            }
        } else if (check(TOKEN_EQUAL)) {
            // Assignment: x = expr
            advance(); // consume =
            expression();

            int arg = resolveLocal(current, &name);
            if (arg != -1) {
                emitBytes(OP_SET_LOCAL, (uint8_t)arg);
            } else if ((arg = resolveUpvalue(current, &name)) != -1) {
                emitBytes(OP_SET_UPVALUE, (uint8_t)arg);
            } else {
                uint8_t global = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
                emitBytes(OP_SET_GLOBAL, global);
            }
            emitByte(OP_POP);
        } else if (check(TOKEN_LEFT_PAREN)) {
            // Could be function call or function declaration
            // Peek ahead: parse params/args, then check for type or '{'

            // Save state before consuming '('
            ScannerState savedScanner;
            Token savedCurrent = parser.current;
            Token savedPrevious = parser.previous;
            saveScannerState(&savedScanner);

            advance(); // consume '('

            // Check if this looks like parameters (identifier followed by type)
            // or arguments (expressions)
            bool looksLikeDeclaration = false;
            if (!check(TOKEN_RIGHT_PAREN)) {
                // First token should be identifier for both cases
                if (check(TOKEN_IDENTIFIER)) {
                    advance(); // consume first identifier
                    // If next is a type keyword, it's a declaration
                    if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
                        check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_BYTE) ||
                        check(TOKEN_VOID) || check(TOKEN_IDENTIFIER)) {
                        looksLikeDeclaration = true;
                    }
                    // Restore parser and scanner state
                    restoreScannerState(&savedScanner);
                    parser.current = savedCurrent;
                    parser.previous = savedPrevious;
                    // Re-consume '(' to be at correct position
                    advance(); // consume '(' again
                }
            } else {
                // Empty parens - could be either, check after ')'
                advance(); // consume ')'
                if (check(TOKEN_LEFT_BRACE) || check(TOKEN_INT) || check(TOKEN_FLOAT) ||
                    check(TOKEN_BOOL) || check(TOKEN_STR) || check(TOKEN_PTR) ||
                    check(TOKEN_VOID)) {
                    // Function declaration with no params
                    // We've consumed both '(' and ')' - compile inline

                    if (current->scopeDepth > 0) {
                        addLocal(name);
                        markInitialized();
                    }

                    Compiler compiler;
                    initCompiler(&compiler, TYPE_FUNCTION);
                    current->function->name = copyString(name.start, name.length);
                    beginScope();
                    // No params to parse - already consumed ')'

                    // Skip return type annotation
                    if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
                        check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_VOID)) {
                        advance();
                    }

                    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
                    block();

                    ObjFunction* fn = endCompiler();
                    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(fn)));

                    for (int i = 0; i < fn->upvalueCount; i++) {
                        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
                        emitByte(compiler.upvalues[i].index);
                    }

                    if (current->scopeDepth == 0) {
                        uint8_t global = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
                        emitBytes(OP_DEFINE_GLOBAL, global);
                    }
                    return;
                } else {
                    // Function call with no args - name()
                    namedVariable(name, false);
                    emitBytes(OP_CALL, 0);
                    emitByte(OP_POP);
                    return;
                }
            }

            if (looksLikeDeclaration) {
                // This is a function declaration
                // We've consumed '(' but not the params
                parser.previous = name;
                funDeclaration(true); // paren already consumed
            } else {
                // This is a function call
                // We already consumed '(', need to parse args and call
                namedVariable(name, false);

                // Parse arguments (similar to argumentList but we already consumed '(')
                uint8_t argCount = 0;
                if (!check(TOKEN_RIGHT_PAREN)) {
                    do {
                        expression();
                        if (argCount == 255) {
                            error("Can't have more than 255 arguments.");
                        }
                        argCount++;
                    } while (match(TOKEN_COMMA));
                }
                consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
                emitBytes(OP_CALL, argCount);
                emitByte(OP_POP);
            }
        } else if (check(TOKEN_LEFT_BRACKET)) {
            // Array subscript access/assignment: arr[0] or arr[0] = value
            namedVariable(name, false);  // Get the array

            advance();  // consume '['
            expression();  // Parse index
            consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

            if (match(TOKEN_EQUAL)) {
                expression();  // Parse value
                emitByte(OP_INDEX_SET);
            } else {
                emitByte(OP_INDEX_GET);
            }
            emitByte(OP_POP);
        } else if (check(TOKEN_DOT)) {
            // Field access/assignment/method call: p.x, p.x = value, p.method()
            namedVariable(name, false);  // Get the struct

            advance();  // consume '.'
            consume(TOKEN_IDENTIFIER, "Expect field name after '.'.");
            uint8_t fieldName = identifierConstant(&parser.previous);

            if (match(TOKEN_EQUAL)) {
                expression();  // Parse value
                emitBytes(OP_SET_FIELD, fieldName);
            } else if (check(TOKEN_LEFT_PAREN)) {
                // Method call
                emitBytes(OP_GET_FIELD, fieldName);
                advance();  // consume '('
                uint8_t argCount = 0;
                if (!check(TOKEN_RIGHT_PAREN)) {
                    do {
                        expression();
                        if (argCount == 255) {
                            error("Can't have more than 255 arguments.");
                        }
                        argCount++;
                    } while (match(TOKEN_COMMA));
                }
                consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
                emitBytes(OP_CALL, argCount);
            } else {
                emitBytes(OP_GET_FIELD, fieldName);
            }
            emitByte(OP_POP);
        } else {
            // Just an expression statement starting with identifier
            namedVariable(name, true);
            emitByte(OP_POP);
        }
    } else {
        expressionStatement();
    }
}

static void function(FunctionType type, bool parenConsumed) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    if (!parenConsumed) {
        consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    }

    // Parameters
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name.");
            (void)constant;

            // Skip type annotation
            if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
                check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_IDENTIFIER)) {
                advance();
            }

            markInitialized();
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");

    // Skip return type annotation
    if (check(TOKEN_INT) || check(TOKEN_FLOAT) || check(TOKEN_BOOL) ||
        check(TOKEN_STR) || check(TOKEN_PTR) || check(TOKEN_VOID) ||
        check(TOKEN_IDENTIFIER)) {
        advance();
    }

    // Body
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(fn)));

    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void funDeclaration(bool parenConsumed) {
    Token name = parser.previous;

    if (current->scopeDepth > 0) {
        addLocal(name);
        markInitialized();
    }

    function(TYPE_FUNCTION, parenConsumed);

    if (current->scopeDepth == 0) {
        uint8_t global = makeConstant(OBJ_VAL(copyString(name.start, name.length)));
        emitBytes(OP_DEFINE_GLOBAL, global);
    }
}

static void declaration(void) {
    statement();

    if (parser.panicMode) {
        // Synchronize
        parser.panicMode = false;
        while (parser.current.type != TOKEN_EOF) {
            if (check(TOKEN_IF) || check(TOKEN_FOR) || check(TOKEN_RETURN) ||
                check(TOKEN_LEFT_BRACE)) {
                return;
            }
            advance();
        }
    }
}

ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}
