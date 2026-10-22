#ifndef ARRAY_SLICE_H
#define ARRAY_SLICE_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/**
 * A view of consecutive items of an array: where they start and how many there are. It owns
 * nothing and is passed by value, and it keeps the pointer and the count together, so that a
 * function cannot be given the count of another array or forget to check one.
 *
 * C has no generics, so the type of the items is not part of the slice: each function says in
 * its documentation what its slice holds, and reads the items through a pointer of that type.
 * What the count means depends on the function too: the items a caller offers, or the room it
 * has for the items it asks to be loaded.
 */
typedef struct Array_Slice {
    void *items;
    int32_t count;
} Array_Slice;

/**
 * The slice of count items starting at items. items may be NULL only when count is 0, as for a
 * list with nothing in it.
 */
static inline Array_Slice array_slice_make(void *items, int32_t count) {
    assert(count >= 0);
    if (count > 0) {
        assert(items != NULL);
    }
    return (Array_Slice){ .items = items, .count = count };
}

#endif
