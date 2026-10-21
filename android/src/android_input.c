#include <zeman/android_input.h>

#include <assert.h>
#include <stddef.h>

#include <jni.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

// Defined by raylib's Android platform code.
struct android_app *GetAndroidApp(void);

static int32_t android_input_callback(struct android_app *app, AInputEvent *event);
static void android_input_push(Android_Input *input, int32_t character);
static int32_t android_key_character(int32_t key_code, int32_t meta_state);
static void android_keyboard_jni(struct android_app *app, bool visible);
static bool android_keyboard_find(JNIEnv *env, jobject activity, jobject *manager,
                                  jobject *decor_view);
static bool jni_failed(JNIEnv *env, const void *result);

void android_input_install(Android_Input *input) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    assert(app->onInputEvent != NULL);
    // The app structure carries the state to the callback and to android_input_char_pressed;
    // raylib leaves userData unused.
    assert(app->userData == NULL);
    *input = (Android_Input){ .raylib_callback = app->onInputEvent };
    app->userData = input;
    app->onInputEvent = android_input_callback;
}

int android_input_char_pressed(void) {
    struct android_app *app = GetAndroidApp();
    Android_Input *input = NULL;
    if (app != NULL) {
        input = app->userData;
    }
    if (input == NULL || input->queue_count == 0) {
        return 0;
    }
    int32_t character = input->queue[input->queue_start];
    input->queue_start = (input->queue_start + 1) % ANDROID_INPUT_QUEUE_SIZE;
    input->queue_count--;
    return character;
}

void android_input_keyboard_set_visible(Android_Input *input, bool visible) {
    if (input->keyboard_visible == visible) {
        return;
    }
    input->keyboard_visible = visible;
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    // The native call works on some devices only; the Java call works on the rest.
    if (visible) {
        ANativeActivity_showSoftInput(app->activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_FORCED);
    } else {
        ANativeActivity_hideSoftInput(app->activity, 0);
    }
    android_keyboard_jni(app, visible);
}

const char *android_input_data_directory(void) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    return app->activity->internalDataPath;
}

/**
 * Queues the character of each key press, then lets raylib handle the event as usual, so that
 * keys such as Backspace and Enter still reach raygui.
 */
static int32_t android_input_callback(struct android_app *app, AInputEvent *event) {
    Android_Input *input = app->userData;
    assert(input != NULL);
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
                android_input_push(input, character);
            }
        }
    }
    return input->raylib_callback(app, event);
}

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
static const Key_Symbol key_symbols[] = {
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
enum { KEY_SYMBOL_COUNT = sizeof(key_symbols) / sizeof(key_symbols[0]) };

/**
 * The character a key types on a US keyboard, or 0 for keys that type none.
 */
static int32_t android_key_character(int32_t key_code, int32_t meta_state) {
    const bool shift = (meta_state & AMETA_SHIFT_ON) != 0;
    const bool caps_lock = (meta_state & AMETA_CAPS_LOCK_ON) != 0;
    if (key_code >= AKEYCODE_A && key_code <= AKEYCODE_Z) {
        int32_t offset = key_code - AKEYCODE_A;
        // Caps Lock and Shift cancel each other out, as on a keyboard.
        if (shift != caps_lock) {
            return 'A' + offset;
        }
        return 'a' + offset;
    }
    if (key_code >= AKEYCODE_0 && key_code <= AKEYCODE_9) {
        static const char shifted_digits[] = ")!@#$%^&*(";
        int32_t digit = key_code - AKEYCODE_0;
        if (shift) {
            return shifted_digits[digit];
        }
        return '0' + digit;
    }
    for (int32_t i = 0; i < KEY_SYMBOL_COUNT; i++) {
        if (key_symbols[i].key_code == key_code) {
            if (shift) {
                return key_symbols[i].shifted;
            }
            return key_symbols[i].plain;
        }
    }
    return 0;
}

/**
 * InputMethodManager.showSoftInput or hideSoftInputFromWindow on the activity's window, through
 * JNI, because NativeActivity has no Java code of its own to do it. A failure only means the
 * keyboard did not change, so it is not reported.
 */
static void android_keyboard_jni(struct android_app *app, bool visible) {
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK || env == NULL) {
        return;
    }
    jobject manager = NULL;
    jobject decor_view = NULL;
    if (!android_keyboard_find(env, app->activity->clazz, &manager, &decor_view)) {
        goto detach;
    }
    jclass manager_class = (*env)->FindClass(env, "android/view/inputmethod/InputMethodManager");
    if (jni_failed(env, manager_class)) {
        goto detach;
    }
    if (visible) {
        jmethodID show = (*env)->GetMethodID(env, manager_class, "showSoftInput",
                                             "(Landroid/view/View;I)Z");
        if (jni_failed(env, show)) {
            goto detach;
        }
        (*env)->CallBooleanMethod(env, manager, show, decor_view, 0);
    } else {
        jclass view_class = (*env)->FindClass(env, "android/view/View");
        if (jni_failed(env, view_class)) {
            goto detach;
        }
        jmethodID get_token = (*env)->GetMethodID(env, view_class, "getWindowToken",
                                                  "()Landroid/os/IBinder;");
        if (jni_failed(env, get_token)) {
            goto detach;
        }
        jobject token = (*env)->CallObjectMethod(env, decor_view, get_token);
        jmethodID hide = (*env)->GetMethodID(env, manager_class, "hideSoftInputFromWindow",
                                             "(Landroid/os/IBinder;I)Z");
        if (jni_failed(env, token) || jni_failed(env, hide)) {
            goto detach;
        }
        (*env)->CallBooleanMethod(env, manager, hide, token, 0);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
// The one cleanup label: every path must detach the thread it attached.
detach:
    (*vm)->DetachCurrentThread(vm);
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
static bool jni_failed(JNIEnv *env, const void *result) {
    if (result == NULL || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return true;
    }
    return false;
}
