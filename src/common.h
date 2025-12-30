#ifndef sharo_common_h
#define sharo_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Debug flags
// #define DEBUG_PRINT_CODE
// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_STRESS_GC
// #define DEBUG_LOG_GC

// Optimization flags
// Enable computed goto for faster dispatch (GCC/Clang only)
#if defined(__GNUC__) || defined(__clang__)
#define COMPUTED_GOTO
#endif

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
