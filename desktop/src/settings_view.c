#include <zeman/settings_view.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "raygui.h"

#include <zeman/bound.h>
#include <zeman/text_box.h>

enum {
    // As in the Connected Devices view, so that the two read alike.
    MARGIN_PIXELS = 40,
    LINE_PIXELS = 28,
    // As tall as the calendar form's text boxes and buttons.
    CONTROL_HEIGHT_PIXELS = 32,
    BUTTON_WIDTH_PIXELS = 100,
    GAP_PIXELS = 8,
    // Wide enough for a deep path; wider would only spread a short one over the screen.
    BOX_WIDTH_PIXELS_MAX = 900,
    // Room for "In use: " and the longest path.
    LABEL_SIZE = SETTINGS_PATH_SIZE + 64,
};

static bool settings_view_edit(Settings_View *view, Rectangle box);
static void settings_view_paste(Settings_View *view);
static void settings_view_path_clean(char *path);

void settings_view_init(Settings_View *view, const char *database_path) {
    assert(view != NULL);
    assert(database_path != NULL);
    assert(strlen(database_path) < sizeof(view->path));
    *view = (Settings_View){ .editing = false };
    snprintf(view->path, sizeof(view->path), "%s", database_path);
}

bool settings_view_draw(Settings_View *view, const App *app, const char *database_path,
                        const char *settings_directory, Rectangle area) {
    assert(database_path != NULL);
    assert(settings_directory != NULL);
    const float x = area.x + MARGIN_PIXELS;
    float y = area.y + MARGIN_PIXELS;
    app_draw_text(app, APP_TEXT_HEADING, STR("Settings"), x, y);
    y += 2 * LINE_PIXELS;
    app_draw_text(app, APP_TEXT_NORMAL, STR("Database file"), x, y);
    y += LINE_PIXELS;
    app_draw_text(app, APP_TEXT_DIM,
                  STR("The SQLite file that holds the calendar and problems. "
                      "It must exist already."),
                  x, y);
    y += LINE_PIXELS;
    app_draw_text(app, APP_TEXT_DIM,
                  STR("In Explorer, Shift+right-click the file and choose Copy as path, then paste "
                  "it here with Ctrl+V."),
                  x, y);
    y += LINE_PIXELS + 4;

    const float room = area.width - 2 * MARGIN_PIXELS - GAP_PIXELS - BUTTON_WIDTH_PIXELS;
    const float box_width = float_min(room, (float)BOX_WIDTH_PIXELS_MAX);
    const Rectangle box = { x, y, box_width, CONTROL_HEIGHT_PIXELS };
    bool apply = settings_view_edit(view, box);
    const Rectangle button = { x + box_width + GAP_PIXELS, y, BUTTON_WIDTH_PIXELS,
                               CONTROL_HEIGHT_PIXELS };
    if (GuiButton(button, "Apply")) {
        apply = true;
    }
    y += CONTROL_HEIGHT_PIXELS + LINE_PIXELS / 2;

    char text[LABEL_SIZE] = "";
    snprintf(text, sizeof(text), "In use: %s", database_path[0] != '\0' ? database_path : "none");
    app_draw_text(app, APP_TEXT_DIM, str_from_cstr(text), x, y);
    y += LINE_PIXELS;
    if (view->message[0] != '\0') {
        app_draw_text(app, APP_TEXT_NORMAL, str_from_cstr(view->message), x, y);
    }
    y += 2 * LINE_PIXELS;
    snprintf(text, sizeof(text), "Settings are saved in %ssettings.ini.", settings_directory);
    app_draw_text(app, APP_TEXT_DIM, str_from_cstr(text), x, y);

    if (apply) {
        view->editing = false;
        settings_view_path_clean(view->path);
    }
    return apply;
}

void settings_view_set_message(Settings_View *view, Str message) {
    // A message that does not fit is cut at a whole character, which beats showing none.
    (void)str_copy_truncated(message, view->message, sizeof(view->message));
}

/**
 * The path's text box. Returns true when Enter is pressed in it, which applies the path as the
 * Apply button does.
 */
static bool settings_view_edit(Settings_View *view, Rectangle box) {
    if (view->editing) {
        settings_view_paste(view);
    }
    // raygui returns true when edit mode should toggle: a click on the box, or Enter or a click
    // elsewhere while editing. Only Enter applies; a click elsewhere just stops editing.
    if (!GuiTextBox(box, view->path, (int)sizeof(view->path), view->editing)) {
        return false;
    }
    const bool was_editing = view->editing;
    view->editing = !view->editing;
    if (!was_editing) {
        return false;
    }
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

/**
 * Ctrl+V pastes at the cursor, as in the calendar's form, because a path is long to type and
 * raygui's text box has no clipboard of its own. GLFW drops the control character Ctrl+V types,
 * so the text box itself sees nothing of it.
 */
static void settings_view_paste(Settings_View *view) {
    assert(view->editing);
    if (!IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_CONTROL)) {
        return;
    }
    if (!IsKeyPressed(KEY_V)) {
        return;
    }
    // NULL when the clipboard is empty, holds no text, or does not exist.
    const char *clipboard = GetClipboardText();
    if (clipboard == NULL) {
        return;
    }
    const int32_t length = (int32_t)strlen(view->path);
    int32_t cursor_index = text_box_cursor_index();
    if (cursor_index > length) {
        cursor_index = length; // raygui clamps it only when it next draws the box.
    }
    text_box_cursor_index_set(text_box_paste(view->path, (int32_t)sizeof(view->path),
                                             cursor_index, str_from_cstr(clipboard)));
}

/**
 * Trims spaces from both ends, then one pair of double quotes around the whole path, which
 * Explorer's "Copy as path" adds and which no Windows file name can hold.
 */
static void settings_view_path_clean(char *path) {
    Str cleaned = str_trim_any(str_from_cstr(path), STR(" "));
    if (cleaned.length >= 2 && cleaned.data[0] == '"' && cleaned.data[cleaned.length - 1] == '"') {
        cleaned = str_slice(cleaned, 1, cleaned.length - 1);
    }
    // The cleaned text lies within path and is never longer, so it moves down in place.
    memmove(path, cleaned.data, cleaned.length);
    path[cleaned.length] = '\0';
}
