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
    // Backspace presses not yet shown to raygui, and whether this frame has one.
    int32_t backspace_waiting;
    bool backspace_now;
    bool keyboard_visible;
} Android_Input;

/**
 * Call after InitWindow, which installs raylib's own input handling; that keeps working.
 */
void android_input_install(Android_Input *input);
/**
 * The next typed character, or 0 when none is left. raygui calls this in place of
 * GetCharPressed (see raygui_impl.c), so it takes no arguments and finds the state through the
 * Android app.
 */
int android_input_char_pressed(void);
/**
 * Call once at the start of each frame: makes one waiting Backspace press visible to
 * android_input_key_pressed for this frame. The on-screen keyboard sends a key's press and
 * release together, so raylib's own key state is already released by the time a frame reads it.
 */
void android_input_frame_begin(Android_Input *input);
/**
 * raygui calls this in place of IsKeyPressed (see raygui_impl.c): raylib's answer, or true for
 * Backspace when the keyboard sent one this frame.
 */
bool android_input_key_pressed(int key);
/**
 * Shows or hides the on-screen keyboard; does nothing when it is already so.
 */
void android_input_keyboard_set_visible(Android_Input *input, bool visible);
/**
 * Where the app may keep its own files, such as the desktop's address.
 */
char const *android_input_data_directory(void);

#endif
