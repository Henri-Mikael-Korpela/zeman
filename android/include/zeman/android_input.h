#ifndef ANDROID_INPUT_H
#define ANDROID_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <pthread.h>

#include <android/input.h>

struct android_app;

// Typing on Android, which raylib leaves out: raylib turns the on-screen keyboard's key events
// into key presses, but never into characters, and never shows the keyboard.
//
// The on-screen keyboard's text comes from MainActivity.java as Unicode code points, so any
// character the keyboard types arrives, such as a-umlaut on a Finnish keyboard. Keys of a
// hardware keyboard are worked out from each key event's key code and shift state, as on a US
// keyboard, so only ASCII comes that way.

enum {
    // Characters typed within one frame, far more than anyone types that fast.
    ANDROID_INPUT_QUEUE_SIZE = 64,
};

typedef struct Android_Input {
    int32_t (*raylib_callback)(struct android_app *app, AInputEvent *event);
    void (*raylib_command)(struct android_app *app, int32_t command);
    int32_t queue[ANDROID_INPUT_QUEUE_SIZE];
    int32_t queue_start;
    int32_t queue_count;
    // Backspace presses not yet shown to raygui, and whether this frame has one.
    int32_t backspace_waiting;
    bool backspace_now;
    bool keyboard_visible;
    // Whether the app has a window and the user's attention: false while the app is in the
    // background or covered, when nothing should be drawn.
    bool window_present;
    bool focused;
    // Something happened that may change what is drawn: input, text, or a change of window.
    bool activity;
    // The keyboard's text arrives on the UI thread, the rest on the app's thread.
    pthread_mutex_t lock;
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
 * Whether the app is shown to the user: it has a window and has the focus. raylib stops drawing
 * to a hidden window only by blocking inside its next frame; the app's loop uses this to wait
 * instead of starting a frame.
 */
bool android_input_visible(Android_Input const *input);
/**
 * True once Android asked the app to close.
 */
bool android_input_destroy_requested(void);
/**
 * Whether anything happened since the last call that may change what is drawn: touches, keys,
 * text from the keyboard, or a change of window or focus. Clears it.
 */
bool android_input_activity_take(Android_Input *input);
/**
 * Waits up to the given time, or without end for -1, for Android to send something, and handles
 * what arrives (touches, keys, window changes). It returns sooner when something arrives; this
 * is what the loop does in place of drawing when nothing changed. raylib's own input state is
 * updated as events are handled, but not rolled over; the next drawn frame does that.
 */
void android_input_wait(Android_Input *input, int32_t timeout_milliseconds);
/**
 * Where the app may keep its own files, such as the desktop's address.
 */
char const *android_input_data_directory(void);
/**
 * Lets the system's status bar (time, battery) and navigation bar (the three buttons or the
 * gesture strip) keep their own space instead of being drawn over: raylib asks for a full screen
 * window. Call after InitWindow.
 */
void android_input_system_bars_show(void);
/**
 * The height in pixels that the status bar takes at the top and the navigation bar at the bottom,
 * or 0 for a bar that is not there. Not yet known right after start: ask again each frame.
 */
void android_input_system_bars_height(int32_t *top, int32_t *bottom);

#endif
