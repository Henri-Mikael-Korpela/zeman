// Single translation unit that compiles the header-only raygui implementation.
#include "raylib.h"

#include "android_input.h"

// raylib on Android never produces typed characters, so raygui's text boxes read them from
// android_input instead. Defined after raylib.h, so that only raygui's calls are redirected.
#define GetCharPressed android_input_char_pressed

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
