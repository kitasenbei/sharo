#ifndef sharo_scanner_h
#define sharo_scanner_h

typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_SEMICOLON,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH,
    TOKEN_PERCENT,

    // One or two character tokens
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_COLON, TOKEN_COLON_EQUAL,
    TOKEN_ARROW,          // ->
    TOKEN_QUESTION,       // ? (for optional types)
    TOKEN_AT,             // @ (for @extern)
    TOKEN_AMPERSAND,      // & (for references)

    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_NUMBER_FLOAT,

    // Keywords - Types
    TOKEN_INT, TOKEN_FLOAT, TOKEN_BOOL, TOKEN_STR,
    TOKEN_PTR, TOKEN_BYTE, TOKEN_VOID,

    // Keywords - Values
    TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL,

    // Keywords - Control flow
    TOKEN_IF, TOKEN_ELSE, TOKEN_FOR, TOKEN_IN,
    TOKEN_WHILE, TOKEN_MATCH, TOKEN_RETURN,
    TOKEN_BREAK, TOKEN_CONTINUE,

    // Keywords - Declarations
    TOKEN_TYPE, TOKEN_EXTERN, TOKEN_SELF,
    TOKEN_IMPORT, TOKEN_EXPORT,

    // Keywords - Logical
    TOKEN_AND, TOKEN_OR, TOKEN_NOT,

    // Special
    TOKEN_ERROR,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
} Token;

typedef struct {
    const char* start;
    const char* current;
    int line;
} ScannerState;

void initScanner(const char* source);
Token scanToken(void);
const char* tokenTypeName(TokenType type);
void saveScannerState(ScannerState* state);
void restoreScannerState(ScannerState* state);

#endif
