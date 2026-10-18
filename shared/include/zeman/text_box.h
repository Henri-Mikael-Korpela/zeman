#ifndef TEXT_BOX_H
#define TEXT_BOX_H

#include <stdint.h>

// The cursor of the raygui text box being edited, as a byte index into its text. raygui keeps
// it in a static variable of its implementation, so each app's raygui_impl.c, which compiles
// that implementation, defines these. The calendar needs the cursor to paste where it is.
int32_t text_box_cursor_index(void);
void text_box_cursor_index_set(int32_t index);

#endif
