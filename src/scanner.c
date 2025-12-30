#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
    const char* start;
    const char* current;
    int line;
} Scanner;

Scanner scanner;

void initScanner(const char* source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool isAtEnd(void) {
    return *scanner.current == '\0';
}

static char advance(void) {
    scanner.current++;
    return scanner.current[-1];
}

static char peek(void) {
    return *scanner.current;
}

static char peekNext(void) {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skipWhitespace(void) {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    // Single-line comment
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if (peekNext() == '*') {
                    // Block comment
                    advance(); // consume /
                    advance(); // consume *
                    int depth = 1;
                    while (depth > 0 && !isAtEnd()) {
                        if (peek() == '/' && peekNext() == '*') {
                            advance();
                            advance();
                            depth++;
                        } else if (peek() == '*' && peekNext() == '/') {
                            advance();
                            advance();
                            depth--;
                        } else {
                            if (peek() == '\n') scanner.line++;
                            advance();
                        }
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(void) {
    // Trie-based keyword recognition
    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'o': return checkKeyword(2, 2, "ol", TOKEN_BOOL);
                    case 'r': return checkKeyword(2, 3, "eak", TOKEN_BREAK);
                    case 'y': return checkKeyword(2, 2, "te", TOKEN_BYTE);
                }
            }
            break;
        case 'c': return checkKeyword(1, 7, "ontinue", TOKEN_CONTINUE);
        case 'e':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l': return checkKeyword(2, 2, "se", TOKEN_ELSE);
                    case 'x': return checkKeyword(2, 4, "tern", TOKEN_EXTERN);
                }
            }
            break;
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'l': return checkKeyword(2, 3, "oat", TOKEN_FLOAT);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'f':
                        if (scanner.current - scanner.start == 2) return TOKEN_IF;
                        break;
                    case 'n':
                        if (scanner.current - scanner.start == 2) return TOKEN_IN;
                        return checkKeyword(2, 1, "t", TOKEN_INT);
                }
            }
            break;
        case 'm': return checkKeyword(1, 4, "atch", TOKEN_MATCH);
        case 'n':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'i': return checkKeyword(2, 1, "l", TOKEN_NIL);
                    case 'o': return checkKeyword(2, 1, "t", TOKEN_NOT);
                }
            }
            break;
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(1, 2, "tr", TOKEN_PTR);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's': return checkKeyword(1, 2, "tr", TOKEN_STR);
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                    case 'y': return checkKeyword(2, 2, "pe", TOKEN_TYPE);
                }
            }
            break;
        case 'v': return checkKeyword(1, 3, "oid", TOKEN_VOID);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token number(void) {
    bool isFloat = false;

    // Check for hex: 0x...
    if (scanner.start[0] == '0' && (peek() == 'x' || peek() == 'X')) {
        advance(); // consume 'x'
        while (isHexDigit(peek())) advance();
        return makeToken(TOKEN_NUMBER);
    }

    // Check for binary: 0b...
    if (scanner.start[0] == '0' && (peek() == 'b' || peek() == 'B')) {
        advance(); // consume 'b'
        while (peek() == '0' || peek() == '1') advance();
        return makeToken(TOKEN_NUMBER);
    }

    while (isDigit(peek())) advance();

    // Look for decimal part
    if (peek() == '.' && isDigit(peekNext())) {
        isFloat = true;
        advance(); // consume '.'
        while (isDigit(peek())) advance();
    }

    // Look for exponent
    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (isDigit(peek())) advance();
    }

    return makeToken(isFloat ? TOKEN_NUMBER_FLOAT : TOKEN_NUMBER);
}

static Token string(void) {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        if (peek() == '\\' && peekNext() != '\0') {
            advance(); // skip backslash
        }
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    advance(); // closing quote
    return makeToken(TOKEN_STRING);
}

Token scanToken(void) {
    skipWhitespace();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();

    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case '[': return makeToken(TOKEN_LEFT_BRACKET);
        case ']': return makeToken(TOKEN_RIGHT_BRACKET);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case '+': return makeToken(TOKEN_PLUS);
        case '*': return makeToken(TOKEN_STAR);
        case '/': return makeToken(TOKEN_SLASH);
        case '%': return makeToken(TOKEN_PERCENT);
        case '?': return makeToken(TOKEN_QUESTION);
        case '@': return makeToken(TOKEN_AT);
        case '&': return makeToken(TOKEN_AMPERSAND);

        case '-':
            return makeToken(match('>') ? TOKEN_ARROW : TOKEN_MINUS);
        case '!':
            return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<':
            return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case ':':
            return makeToken(match('=') ? TOKEN_COLON_EQUAL : TOKEN_COLON);

        case '"': return string();
    }

    return errorToken("Unexpected character.");
}

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TOKEN_LEFT_PAREN: return "LEFT_PAREN";
        case TOKEN_RIGHT_PAREN: return "RIGHT_PAREN";
        case TOKEN_LEFT_BRACE: return "LEFT_BRACE";
        case TOKEN_RIGHT_BRACE: return "RIGHT_BRACE";
        case TOKEN_LEFT_BRACKET: return "LEFT_BRACKET";
        case TOKEN_RIGHT_BRACKET: return "RIGHT_BRACKET";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_DOT: return "DOT";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_STAR: return "STAR";
        case TOKEN_SLASH: return "SLASH";
        case TOKEN_PERCENT: return "PERCENT";
        case TOKEN_BANG: return "BANG";
        case TOKEN_BANG_EQUAL: return "BANG_EQUAL";
        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TOKEN_GREATER: return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
        case TOKEN_LESS: return "LESS";
        case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
        case TOKEN_COLON: return "COLON";
        case TOKEN_COLON_EQUAL: return "COLON_EQUAL";
        case TOKEN_ARROW: return "ARROW";
        case TOKEN_QUESTION: return "QUESTION";
        case TOKEN_AT: return "AT";
        case TOKEN_AMPERSAND: return "AMPERSAND";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_NUMBER_FLOAT: return "NUMBER_FLOAT";
        case TOKEN_INT: return "INT";
        case TOKEN_FLOAT: return "FLOAT";
        case TOKEN_BOOL: return "BOOL";
        case TOKEN_STR: return "STR";
        case TOKEN_PTR: return "PTR";
        case TOKEN_BYTE: return "BYTE";
        case TOKEN_VOID: return "VOID";
        case TOKEN_TRUE: return "TRUE";
        case TOKEN_FALSE: return "FALSE";
        case TOKEN_NIL: return "NIL";
        case TOKEN_IF: return "IF";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_FOR: return "FOR";
        case TOKEN_IN: return "IN";
        case TOKEN_WHILE: return "WHILE";
        case TOKEN_MATCH: return "MATCH";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_BREAK: return "BREAK";
        case TOKEN_CONTINUE: return "CONTINUE";
        case TOKEN_TYPE: return "TYPE";
        case TOKEN_EXTERN: return "EXTERN";
        case TOKEN_AND: return "AND";
        case TOKEN_OR: return "OR";
        case TOKEN_NOT: return "NOT";
        case TOKEN_ERROR: return "ERROR";
        case TOKEN_EOF: return "EOF";
        default: return "UNKNOWN";
    }
}

void saveScannerState(ScannerState* state) {
    state->start = scanner.start;
    state->current = scanner.current;
    state->line = scanner.line;
}

void restoreScannerState(ScannerState* state) {
    scanner.start = state->start;
    scanner.current = state->current;
    scanner.line = state->line;
}
