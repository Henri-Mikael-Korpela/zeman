// Single translation unit that compiles the header-only raygui implementation.
#include <assert.h>
#include <stdint.h>

#include "raylib.h"

#include "android_input.h"

// raylib on Android never produces typed characters, so raygui's text boxes read them from
// android_input instead. Defined after raylib.h, so that only raygui's calls are redirected.
#define GetCharPressed android_input_char_pressed

#define RAYGUI_IMPLEMENTATION
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
