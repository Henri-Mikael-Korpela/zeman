#ifndef TEXT_BOX_H
#define TEXT_BOX_H

#include <stdbool.h>
#include <stdint.h>

#include <zeman/str.h>

enum {
    // The largest text box text_box_paste fills, terminator included: room for a long file path
    // in UTF-8, and well above the longest calendar field.
    TEXT_BOX_SIZE_MAX = 1024,
};

/**
 * The cursor of the raygui text box being edited, as a byte index into its text. raygui keeps
 * it in a static variable of its implementation, so each app's raygui_impl.c, which compiles
 * that implementation, defines these. The calendar needs the cursor to paste where it is.
 */
int32_t text_box_cursor_index(void);
void text_box_cursor_index_set(int32_t index);

/**
 * The next character typed this frame, as a Unicode code point, or 0 when none is left; a line
 * break is '\n' where the platform types one as a character. For text that the calendar edits
 * itself rather than through raygui, such as a description. Each app's raygui_impl.c defines it
 * as raygui reads typing there, because Android delivers typing in its own way.
 */
int32_t text_input_char_pressed(void);

/**
 * Whether the key was pressed this frame, or repeats while held, as text editing wants it.
 * Defined beside text_input_char_pressed, for the same reason.
 */
bool text_input_key_pressed(int32_t key);

/**
 * Inserts the clipboard's text into text at cursor_index as one line, because every text box is
 * one line: line breaks and tabs become spaces, and other control characters and broken UTF-8
 * are left out. What does not fit is cut at a whole character. Returns the index after the
 * pasted text. Defined in shared/src/text_box.c, for every app.
 */
int32_t text_box_paste(char *text, int32_t text_size, int32_t cursor_index, Str clipboard);

#endif
