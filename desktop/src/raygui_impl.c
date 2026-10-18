// Single translation unit that compiles the header-only raygui implementation.
#include <assert.h>
#include <stdint.h>

#define RAYGUI_IMPLEMENTATION
#include "raylib.h"
#include "raygui.h"

#include "text_box.h"

// raygui's cursor is a static of the implementation above, so only this file can reach it.
// raygui clamps it to the text's length itself, so any index from 0 up is safe to set.
int32_t text_box_cursor_index(void) {
    return (int32_t)textBoxCursorIndex;
}

void text_box_cursor_index_set(int32_t index) {
    assert(index >= 0);
    textBoxCursorIndex = (int)index;
}
