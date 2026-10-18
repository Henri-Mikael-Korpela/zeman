#ifndef ANDROID_PERMISSION_H
#define ANDROID_PERMISSION_H

#include <stdbool.h>

// The camera permission, which Android asks the user for while the app runs. NativeActivity has
// no Java code, so this goes through JNI; the answer is not delivered to native code, so callers
// ask android_camera_permitted again until it is true.

bool android_camera_permitted(void);
// Shows Android's dialog asking to allow the camera.
void android_camera_permission_request(void);

#endif
