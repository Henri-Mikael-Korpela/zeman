#include "android_permission.h"

#include <assert.h>
#include <stddef.h>

#include <jni.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

// Defined by raylib's Android platform code.
struct android_app *GetAndroidApp(void);

static const char CAMERA_PERMISSION[] = "android.permission.CAMERA";
// Identifies the request in Android's answer, which native code does not receive anyway.
static const jint REQUEST_CODE = 1;

static JNIEnv *permission_attach(JavaVM *vm);
static bool permission_failed(JNIEnv *env, const void *result);

bool android_camera_permitted(void) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = permission_attach(vm);
    if (env == NULL) {
        return false;
    }
    bool permitted = false;
    jobject activity = app->activity->clazz;
    jclass activity_class = (*env)->GetObjectClass(env, activity);
    jstring name = (*env)->NewStringUTF(env, CAMERA_PERMISSION);
    if (!permission_failed(env, activity_class) && !permission_failed(env, name)) {
        jmethodID check = (*env)->GetMethodID(env, activity_class, "checkSelfPermission",
                                              "(Ljava/lang/String;)I");
        if (!permission_failed(env, check)) {
            // PackageManager.PERMISSION_GRANTED is 0.
            jint result = (*env)->CallIntMethod(env, activity, check, name);
            permitted = !(*env)->ExceptionCheck(env) && result == 0;
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    (*vm)->DetachCurrentThread(vm);
    return permitted;
}

void android_camera_permission_request(void) {
    struct android_app *app = GetAndroidApp();
    assert(app != NULL);
    JavaVM *vm = app->activity->vm;
    JNIEnv *env = permission_attach(vm);
    if (env == NULL) {
        return;
    }
    jobject activity = app->activity->clazz;
    jclass activity_class = (*env)->GetObjectClass(env, activity);
    jclass string_class = (*env)->FindClass(env, "java/lang/String");
    jstring name = (*env)->NewStringUTF(env, CAMERA_PERMISSION);
    bool ready = !permission_failed(env, activity_class) &&
                 !permission_failed(env, string_class) && !permission_failed(env, name);
    if (ready) {
        jobjectArray names = (*env)->NewObjectArray(env, 1, string_class, name);
        jmethodID request = (*env)->GetMethodID(env, activity_class, "requestPermissions",
                                                "([Ljava/lang/String;I)V");
        if (!permission_failed(env, names) && !permission_failed(env, request)) {
            (*env)->CallVoidMethod(env, activity, request, names, REQUEST_CODE);
        }
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
    (*vm)->DetachCurrentThread(vm);
}

static JNIEnv *permission_attach(JavaVM *vm) {
    JNIEnv *env = NULL;
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    return env;
}

// True, with the exception cleared, when a JNI call failed, so that no later call runs with an
// exception pending.
static bool permission_failed(JNIEnv *env, const void *result) {
    if (result == NULL || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return true;
    }
    return false;
}
