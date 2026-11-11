// Single translation unit that compiles the header-only raygui implementation.
#include <assert.h>
#include <stdint.h>

#define RAYGUI_IMPLEMENTATION
#include "raylib.h"
#include "raygui.h"

#include <zeman/text_box.h>

/**
 * raygui's cursor is a static of the implementation above, so only this file can reach it.
 * raygui clamps it to the text's length itself, so any index from 0 up is safe to set.
 */
int32_t text_box_cursor_index(void) {
    return (int32_t)textBoxCursorIndex;
}

void text_box_cursor_index_set(int32_t index) {
    assert(index >= 0);
    textBoxCursorIndex = (int)index;
}

int32_t text_input_char_pressed(void) {
    return (int32_t)GetCharPressed();
}

/**
 * GLFW repeats a held key, which raylib reports apart from the first press.
 */
bool text_input_key_pressed(int32_t key) {
    return IsKeyPressed((int)key) || IsKeyPressedRepeat((int)key);
}
