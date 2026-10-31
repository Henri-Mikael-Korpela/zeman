#include <zeman/android_input.h>

#include <assert.h>
#include <stddef.h>
#include <pthread.h>

#include <raylib.h>

#include <jni.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <android_native_app_glue.h>

// Defined by raylib's Android platform code.
struct android_app *GetAndroidApp(void);

static int32_t android_input_callback(struct android_app *app, AInputEvent *event);
static void android_input_command(struct android_app *app, int32_t command);
static void android_input_wake(Android_Input *input);
static void android_input_push(Android_Input *input, int32_t character);
static Android_Input *android_input_get(void);
static int32_t android_key_character(int32_t key_code, int32_t meta_state);
static void android_keyboard_jni(struct android_app *app, bool visible);
static bool android_keyboard_find(JNIEnv *env, jobject activity, jobject *manager,
                                  jobject *decor_view);
static bool jni_failed(JNIEnv *env, void const *result);

void android_input_install(Android_Input *input) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    assert(app->onInputEvent != NULL);
    // The app structure carries the state to the callback and to android_input_char_pressed;
    // raylib leaves userData unused.
    assert(app->userData == NULL);
    assert(app->onAppCmd != NULL);
    // The window and the focus are there already: InitWindow waits for them.
    *input = (Android_Input){ .raylib_callback = app->onInputEvent,
                             .raylib_command = app->onAppCmd,
                             .lock = PTHREAD_MUTEX_INITIALIZER,
                             .window_present = app->window != NULL,
                             .focused = true,
                             .activity = true };
    app->userData = input;
    app->onInputEvent = android_input_callback;
    app->onAppCmd = android_input_command;
}

bool android_input_visible(Android_Input const *input) {
    return input->window_present && input->focused;
}

bool android_input_destroy_requested(void) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    return app->destroyRequested != 0;
}

bool android_input_activity_take(Android_Input *input) {
    pthread_mutex_lock(&input->lock);
    bool activity = input->activity;
    input->activity = false;
    pthread_mutex_unlock(&input->lock);
    return activity;
}

void android_input_wait(Android_Input *input, int32_t timeout_milliseconds) {
    (void)input;
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    int events = 0;
    struct android_poll_source *source = NULL;
    // The first poll waits; the rest only take what has arrived by then, so that events that
    // came together are read together, as raylib reads them while drawing.
    int32_t timeout = timeout_milliseconds;
    int result = 0;
    while ((result = ALooper_pollOnce(timeout, NULL, &events, (void **)&source)) >= 0) {
        timeout = 0;
        if (source != NULL) {
            source->process(app, source);
        }
        source = NULL;
    }
}

int android_input_char_pressed(void) {
    Android_Input *input = android_input_get();
    if (input == NULL) {
        return 0;
    }
    pthread_mutex_lock(&input->lock);
    int32_t character = 0;
    if (input->queue_count > 0) {
        character = input->queue[input->queue_start];
        input->queue_start = (input->queue_start + 1) % ANDROID_INPUT_QUEUE_SIZE;
        input->queue_count--;
    }
    pthread_mutex_unlock(&input->lock);
    return character;
}

void android_input_frame_begin(Android_Input *input) {
    pthread_mutex_lock(&input->lock);
    input->backspace_now = input->backspace_waiting > 0;
    if (input->backspace_now) {
        input->backspace_waiting--;
    }
    pthread_mutex_unlock(&input->lock);
}

bool android_input_key_pressed(int key) {
    if (IsKeyPressed(key)) {
        return true;
    }
    Android_Input const *input = android_input_get();
    return key == KEY_BACKSPACE && input != NULL && input->backspace_now;
}

void android_input_keyboard_set_visible(Android_Input *input, bool visible) {
    if (input->keyboard_visible == visible) {
        return;
    }
    input->keyboard_visible = visible;
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    android_keyboard_jni(app, visible);
}

char const *android_input_data_directory(void) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    return app->activity->internalDataPath;
}

void android_input_system_bars_show(void) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    // raylib sets AWINDOW_FLAG_FULLSCREEN, which hides the status bar.
    ANativeActivity_setWindowFlags(app->activity, 0, AWINDOW_FLAG_FULLSCREEN);
}

void android_input_system_bars_height(int32_t *top, int32_t *bottom) {
    *top = 0;
    *bottom = 0;
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK || env == NULL) {
        return;
    }
    jobject manager = NULL;
    jobject decor_view = NULL;
    // The manager is not needed; only the same window lookup.
    if (!android_keyboard_find(env, app->activity->clazz, &manager, &decor_view)) {
        goto detach;
    }
    jclass view_class = (*env)->FindClass(env, "android/view/View");
    if (jni_failed(env, view_class)) {
        goto detach;
    }
    jmethodID get_insets = (*env)->GetMethodID(env, view_class, "getRootWindowInsets",
                                               "()Landroid/view/WindowInsets;");
    if (jni_failed(env, get_insets)) {
        goto detach;
    }
    jobject insets = (*env)->CallObjectMethod(env, decor_view, get_insets);
    jclass insets_class = (*env)->FindClass(env, "android/view/WindowInsets");
    if (jni_failed(env, insets) || jni_failed(env, insets_class)) {
        goto detach;
    }
    // The stable insets leave out the on-screen keyboard, which would otherwise move the layout.
    jmethodID get_top = (*env)->GetMethodID(env, insets_class, "getStableInsetTop", "()I");
    jmethodID get_bottom = (*env)->GetMethodID(env, insets_class, "getStableInsetBottom", "()I");
    if (jni_failed(env, get_top) || jni_failed(env, get_bottom)) {
        goto detach;
    }
    *top = (*env)->CallIntMethod(env, insets, get_top);
    *bottom = (*env)->CallIntMethod(env, insets, get_bottom);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        *top = 0;
        *bottom = 0;
    }
// The one cleanup label: every path must detach the thread it attached.
detach:
    (*vm)->DetachCurrentThread(vm);
}

/**
 * Queues the character of each key press, then lets raylib handle the event as usual, so that
 * keys such as Backspace and Enter still reach raygui.
 */
static int32_t android_input_callback(struct android_app *app, AInputEvent *event) {
    Android_Input *input = app->userData;
    assert(input != NULL);
    pthread_mutex_lock(&input->lock);
    input->activity = true;
    pthread_mutex_unlock(&input->lock);
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t action = AKeyEvent_getAction(event);
        if (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_MULTIPLE) {
            int32_t character = android_key_character(AKeyEvent_getKeyCode(event),
                                                      AKeyEvent_getMetaState(event));
            // A held key arrives as one event with a repeat count.
            int32_t count = 1;
            if (action == AKEY_EVENT_ACTION_MULTIPLE) {
                count = AKeyEvent_getRepeatCount(event);
            }
            for (int32_t i = 0; i < count && i < ANDROID_INPUT_QUEUE_SIZE; i++) {
                pthread_mutex_lock(&input->lock);
                android_input_push(input, character);
                if (AKeyEvent_getKeyCode(event) == AKEYCODE_DEL &&
                    input->backspace_waiting < ANDROID_INPUT_QUEUE_SIZE) {
                    input->backspace_waiting++;
                }
                pthread_mutex_unlock(&input->lock);
            }
        }
    }
    return input->raylib_callback(app, event);
}

/**
 * Runs raylib's handling of a change of the app's window or focus, which creates and destroys its
 * graphics, then notes what it means for drawing.
 */
static void android_input_command(struct android_app *app, int32_t command) {
    Android_Input *input = app->userData;
    assert(input != NULL);
    input->raylib_command(app, command);
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        input->window_present = true;
        break;
    case APP_CMD_TERM_WINDOW:
        input->window_present = false;
        break;
    case APP_CMD_GAINED_FOCUS:
        input->focused = true;
        break;
    case APP_CMD_LOST_FOCUS:
        input->focused = false;
        break;
    default:
        break;
    }
    pthread_mutex_lock(&input->lock);
    input->activity = true;
    pthread_mutex_unlock(&input->lock);
}

/**
 * For the UI thread, after it queued text: marks activity and wakes the app's thread from its
 * wait, so that the text shows at once.
 */
static void android_input_wake(Android_Input *input) {
    pthread_mutex_lock(&input->lock);
    input->activity = true;
    pthread_mutex_unlock(&input->lock);
    struct android_app *app = GetAndroidApp();
    if (app != NULL && app->looper != NULL) {
        ALooper_wake(app->looper);
    }
}

/**
 * Call with the lock held.
 */
static void android_input_push(Android_Input *input, int32_t character) {
    if (character == 0 || input->queue_count == ANDROID_INPUT_QUEUE_SIZE) {
        return;
    }
    int32_t index = (input->queue_start + input->queue_count) % ANDROID_INPUT_QUEUE_SIZE;
    input->queue[index] = character;
    input->queue_count++;
}

// A key that types a symbol, with what it types alone and with Shift held.
typedef struct Key_Symbol {
    int32_t key_code;
    char plain;
    char shifted;
} Key_Symbol;

// A table rather than a switch, because key codes come from the system and the many codes that
// type nothing are no mistake to assert on, and because each key then states both its symbols
// once. Keys that type the same with and without Shift list it twice.
static Key_Symbol const KEY_SYMBOLS[] = {
    { AKEYCODE_SPACE, ' ', ' ' },
    { AKEYCODE_COMMA, ',', '<' },
    { AKEYCODE_PERIOD, '.', '>' },
    { AKEYCODE_MINUS, '-', '_' },
    { AKEYCODE_EQUALS, '=', '+' },
    { AKEYCODE_LEFT_BRACKET, '[', '{' },
    { AKEYCODE_RIGHT_BRACKET, ']', '}' },
    { AKEYCODE_BACKSLASH, '\\', '|' },
    { AKEYCODE_SEMICOLON, ';', ':' },
    { AKEYCODE_APOSTROPHE, '\'', '"' },
    { AKEYCODE_SLASH, '/', '?' },
    { AKEYCODE_GRAVE, '`', '~' },
    { AKEYCODE_AT, '@', '@' },
    { AKEYCODE_PLUS, '+', '+' },
    { AKEYCODE_STAR, '*', '*' },
    { AKEYCODE_POUND, '#', '#' },
};
enum { KEY_SYMBOL_COUNT = sizeof(KEY_SYMBOLS) / sizeof(KEY_SYMBOLS[0]) };

/**
 * The character a key types on a US keyboard, or 0 for keys that type none.
 */
static int32_t android_key_character(int32_t key_code, int32_t meta_state) {
    bool const shift = (meta_state & AMETA_SHIFT_ON) != 0;
    bool const caps_lock = (meta_state & AMETA_CAPS_LOCK_ON) != 0;
    if (key_code >= AKEYCODE_A && key_code <= AKEYCODE_Z) {
        int32_t offset = key_code - AKEYCODE_A;
        // Caps Lock and Shift cancel each other out, as on a keyboard.
        if (shift != caps_lock) {
            return 'A' + offset;
        }
        return 'a' + offset;
    }
    if (key_code >= AKEYCODE_0 && key_code <= AKEYCODE_9) {
        static char const SHIFTED_DIGITS[] = ")!@#$%^&*(";
        int32_t digit = key_code - AKEYCODE_0;
        if (shift) {
            return SHIFTED_DIGITS[digit];
        }
        return '0' + digit;
    }
    for (int32_t i = 0; i < KEY_SYMBOL_COUNT; i++) {
        if (KEY_SYMBOLS[i].key_code == key_code) {
            if (shift) {
                return KEY_SYMBOLS[i].shifted;
            }
            return KEY_SYMBOLS[i].plain;
        }
    }
    return 0;
}

/**
 * The state installed by android_input_install, or NULL before that.
 */
static Android_Input *android_input_get(void) {
    struct android_app *app = GetAndroidApp();
    if (app == NULL) {
        return NULL;
    }
    return app->userData;
}

/**
 * Calls MainActivity.setKeyboardVisible through JNI: the activity's Java code owns the view that
 * the keyboard types into (see MainActivity.java). A failure only means the keyboard did not
 * change, so it is not reported.
 */
static void android_keyboard_jni(struct android_app *app, bool visible) {
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK || env == NULL) {
        return;
    }
    jclass activity_class = (*env)->GetObjectClass(env, app->activity->clazz);
    if (jni_failed(env, activity_class)) {
        goto detach;
    }
    jmethodID set_visible = (*env)->GetMethodID(env, activity_class, "setKeyboardVisible", "(Z)V");
    if (jni_failed(env, set_visible)) {
        goto detach;
    }
    (*env)->CallVoidMethod(env, app->activity->clazz, set_visible, (jboolean)visible);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
// The one cleanup label: every path must detach the thread it attached.
detach:
    (*vm)->DetachCurrentThread(vm);
}

/**
 * Called by MainActivity's input connection on the UI thread with what the keyboard typed: the
 * UTF-16 text is queued as Unicode code points, so that letters such as a-umlaut and emoji
 * arrive, which key events cannot carry.
 */
JNIEXPORT void JNICALL Java_com_zeman_mobile_MainActivity_nativeTextTyped(JNIEnv *env, jclass clazz,
                                                                          jstring text) {
    (void)clazz;
    Android_Input *input = android_input_get();
    if (input == NULL || text == NULL) {
        return;
    }
    jsize const length = (*env)->GetStringLength(env, text);
    jchar const *units = (*env)->GetStringChars(env, text, NULL);
    if (units == NULL) {
        return;
    }
    pthread_mutex_lock(&input->lock);
    for (jsize i = 0; i < length; i++) {
        int32_t code_point = units[i];
        bool const high = code_point >= 0xD800 && code_point <= 0xDBFF;
        if (high && i + 1 < length && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            i++;
        } else if (code_point >= 0xD800 && code_point <= 0xDFFF) {
            continue;  // A lone surrogate is no character.
        }
        // A new line is not typed text: the keyboard's Enter arrives as a key event.
        if (code_point != '\n') {
            android_input_push(input, code_point);
        }
    }
    pthread_mutex_unlock(&input->lock);
    (*env)->ReleaseStringChars(env, text, units);
    android_input_wake(input);
}

/**
 * Called by MainActivity's input connection when the keyboard deletes text before the cursor.
 */
JNIEXPORT void JNICALL Java_com_zeman_mobile_MainActivity_nativeBackspace(JNIEnv *env, jclass clazz,
                                                                          jint count) {
    (void)env;
    (void)clazz;
    Android_Input *input = android_input_get();
    if (input == NULL) {
        return;
    }
    pthread_mutex_lock(&input->lock);
    for (jint i = 0; i < count && input->backspace_waiting < ANDROID_INPUT_QUEUE_SIZE; i++) {
        input->backspace_waiting++;
    }
    pthread_mutex_unlock(&input->lock);
    android_input_wake(input);
}

/**
 * Finds the activity's InputMethodManager and the top view of its window. Returns false when
 * any step fails.
 */
static bool android_keyboard_find(JNIEnv *env, jobject activity, jobject *manager,
                                  jobject *decor_view) {
    jclass activity_class = (*env)->GetObjectClass(env, activity);
    jclass context_class = (*env)->FindClass(env, "android/content/Context");
    if (jni_failed(env, activity_class) || jni_failed(env, context_class)) {
        return false;
    }
    jfieldID service_field = (*env)->GetStaticFieldID(env, context_class, "INPUT_METHOD_SERVICE",
                                                      "Ljava/lang/String;");
    if (jni_failed(env, service_field)) {
        return false;
    }
    jobject service_name = (*env)->GetStaticObjectField(env, context_class, service_field);
    jmethodID get_system_service = (*env)->GetMethodID(
        env, activity_class, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (jni_failed(env, service_name) || jni_failed(env, get_system_service)) {
        return false;
    }
    *manager = (*env)->CallObjectMethod(env, activity, get_system_service, service_name);
    jmethodID get_window = (*env)->GetMethodID(env, activity_class, "getWindow",
                                               "()Landroid/view/Window;");
    if (jni_failed(env, *manager) || jni_failed(env, get_window)) {
        return false;
    }
    jobject window = (*env)->CallObjectMethod(env, activity, get_window);
    jclass window_class = (*env)->FindClass(env, "android/view/Window");
    if (jni_failed(env, window) || jni_failed(env, window_class)) {
        return false;
    }
    jmethodID get_decor_view = (*env)->GetMethodID(env, window_class, "getDecorView",
                                                   "()Landroid/view/View;");
    if (jni_failed(env, get_decor_view)) {
        return false;
    }
    *decor_view = (*env)->CallObjectMethod(env, window, get_decor_view);
    return !jni_failed(env, *decor_view);
}

/**
 * True, with the exception cleared, when a JNI call failed. Every later call needs the earlier
 * ones to have worked, and JNI must not be called with an exception pending.
 */
static bool jni_failed(JNIEnv *env, void const *result) {
    if (result == NULL || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return true;
    }
    return false;
}
