#ifndef SETTINGS_VIEW_H
#define SETTINGS_VIEW_H

#include <stdbool.h>

#include <zeman/app.h>
#include <zeman/settings.h>

enum {
    // Room for a sentence around the longest settings or database error.
    SETTINGS_VIEW_MESSAGE_SIZE = SETTINGS_ERROR_SIZE + 96,
};

// The Settings view: the path of the database file, which the user can change.
typedef struct Settings_View {
    char path[SETTINGS_PATH_SIZE];             // The text box, as the user edits it
    char message[SETTINGS_VIEW_MESSAGE_SIZE];  // What the last change did, or empty
    bool editing;                              // The text box takes the keys
} Settings_View;

// Fills the text box with the path of the database in use, which may be empty.
void settings_view_init(Settings_View *view, const char *database_path);
// Draws the view into area. database_path is the database in use, or empty when none is open.
// Returns true when the user asks to use the path in the text box, with Apply or Enter; the
// path is then trimmed of spaces and of the quotes Explorer's "Copy as path" adds, and the
// caller switches the database and says how that went with settings_view_set_message.
bool settings_view_draw(Settings_View *view, const App *app, const char *database_path,
                        const char *settings_directory, Rectangle area);
void settings_view_set_message(Settings_View *view, const char *message);

#endif
