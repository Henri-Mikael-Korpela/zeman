package com.zeman.mobile;

import android.app.NativeActivity;
import android.content.Context;
import android.os.Bundle;
import android.text.InputType;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;

/**
 * The app's activity. All of the app is C code; this Java class exists only because a keyboard
 * delivers typed text (such as a-umlaut on a Finnish keyboard) through an InputConnection of a
 * view, and native code has no way to provide one. The text is handed to the C code, which
 * queues it for the text boxes (see android_input.c).
 */
public class MainActivity extends NativeActivity {
    private TextInputView textInput;

    static {
        // NativeActivity loads libmain.so on its own, which does not let the native methods below
        // find their C functions; loading it here, by name, does.
        System.loadLibrary("main");
    }

    static native void nativeTextTyped(String text);

    static native void nativeBackspace(int count);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Invisible, and only as large as the keyboard needs to treat it as a text field.
        textInput = new TextInputView(this);
        addContentView(textInput, new FrameLayout.LayoutParams(1, 1));
    }

    /**
     * Called from C on its own thread.
     */
    public void setKeyboardVisible(final boolean visible) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                InputMethodManager manager =
                    (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                if (visible) {
                    textInput.requestFocus();
                    manager.restartInput(textInput);
                    manager.showSoftInput(textInput, 0);
                } else {
                    manager.hideSoftInputFromWindow(textInput.getWindowToken(), 0);
                }
            }
        });
    }

    /**
     * A view that the keyboard types into, with nothing to show.
     */
    private static class TextInputView extends View {
        TextInputView(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo info) {
            // Visible password text and no suggestions keep the keyboard from composing words,
            // so that what it types arrives as it is typed.
            info.inputType = InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
            info.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            return new Connection(this);
        }
    }

    private static class Connection extends BaseInputConnection {
        // Text the keyboard still composes: sent when it is committed or the composing ends.
        private String composing = "";

        Connection(View view) {
            super(view, false);
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            composing = "";
            nativeTextTyped(text.toString());
            return true;
        }

        @Override
        public boolean setComposingText(CharSequence text, int newCursorPosition) {
            composing = text.toString();
            return true;
        }

        @Override
        public boolean finishComposingText() {
            if (!composing.isEmpty()) {
                nativeTextTyped(composing);
                composing = "";
            }
            return true;
        }

        @Override
        public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            if (!composing.isEmpty()) {
                composing = composing.substring(0, composing.length() - 1);
                return true;
            }
            nativeBackspace(beforeLength);
            return true;
        }
    }
}
