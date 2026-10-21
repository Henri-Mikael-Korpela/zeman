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

/**
 * value brought into the range from low to high, both included.
 */
static inline int32_t int32_clamp(int32_t value, int32_t low, int32_t high) {
    assert(low <= high);
    return int32_min(int32_max(value, low), high);
}

static inline float float_min(float a, float b) {
    if (a < b) {
        return a;
    }
    return b;
}

static inline float float_max(float a, float b) {
    if (a > b) {
        return a;
    }
    return b;
}

/**
 * value brought into the range from low to high, both included.
 */
static inline float float_clamp(float value, float low, float high) {
    assert(low <= high);
    return float_min(float_max(value, low), high);
}

#endif
