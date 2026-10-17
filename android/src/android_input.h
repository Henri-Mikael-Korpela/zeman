#ifndef ANDROID_INPUT_H
#define ANDROID_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <android/input.h>

struct android_app;

// Typing on Android, which raylib leaves out: raylib turns the on-screen keyboard's key events
// into key presses, but never into characters, and never shows the keyboard.
//
// Characters are worked out from each key event's key code and shift state, as on a US
// keyboard, so letters, digits and common punctuation work; letters outside ASCII, which
// keyboards send another way, do not.

enum {
    // Characters typed within one frame, far more than anyone types that fast.
    ANDROID_INPUT_QUEUE_SIZE = 64,
};

typedef struct Android_Input {
    int32_t (*raylib_callback)(struct android_app *app, AInputEvent *event);
    int32_t queue[ANDROID_INPUT_QUEUE_SIZE];
    int32_t queue_start;
    int32_t queue_count;
    bool keyboard_visible;
} Android_Input;

// Call after InitWindow, which installs raylib's own input handling; that keeps working.
void android_input_install(Android_Input *input);
// The next typed character, or 0 when none is left. raygui calls this in place of
// GetCharPressed (see raygui_impl.c), so it takes no arguments and finds the state through the
// Android app.
int android_input_char_pressed(void);
// Shows or hides the on-screen keyboard; does nothing when it is already so.
void android_input_keyboard_set_visible(Android_Input *input, bool visible);
// Where the app may keep its own files, such as the desktop's address.
const char *android_input_data_directory(void);

#endif
