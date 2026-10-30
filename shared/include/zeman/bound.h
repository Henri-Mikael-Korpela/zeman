#ifndef BOUND_H
#define BOUND_H

#include <assert.h>
#include <stdint.h>

// Small pure functions that bound a number, in place of the ?: operator, which the project
// avoids: a name says what is meant, and each argument is evaluated exactly once, unlike in a
// macro. static inline, so that the compiler sees through them as it would through ?:.

static inline int32_t int32_min(int32_t a, int32_t b) {
    if (a < b) {
        return a;
    }
    return b;
}

static inline int32_t int32_max(int32_t a, int32_t b) {
    if (a > b) {
        return a;
    }
    return b;
}

static inline float float_min(float a, float b) {
    if (a < b) {
        return a;
    }
    return b;
}

#endif
