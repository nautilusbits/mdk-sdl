#pragma once

#include <jni.h>

#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace Mdk::Android
{
    // The JNI layer's mechanics: the VM, the app's classes (looked up once, on the thread that
    // loaded libmain.so: a native thread's FindClass sees only the system's classes), strings,
    // and exceptions both ways. The rest of the head works with domain calls only.
    //   Java ─native─► Jni.cpp ─► Mdk.Game / Mdk.Formats
    //   C++ ─JavaTree, SetupScreen─► JavaBridge ─JNI─► Java
    struct JavaBridge
    {
        // The app's classes the native code calls into (global references).
        struct Classes
        {
            jclass DataTree = nullptr;
            jclass DataEntry = nullptr;
            jclass Progress = nullptr;
            jclass InputStream = nullptr;
            jclass IOException = nullptr;
            jclass NativeException = nullptr;
            jclass SetupActivity = nullptr;
            jclass MdkData = nullptr;
            jclass StoragePlan = nullptr;
        };

        // JNI_OnLoad: keeps the VM and looks the classes up. Returns false when one is missing.
        static bool Load(JavaVM* vm);

        static const Classes& Types() { return _classes; }

        // The calling thread's environment (attached if it wasn't).
        static JNIEnv* Env();

        // A pending Java exception becomes a JavaException.
        static void Check(JNIEnv* env);

        static std::string ToString(JNIEnv* env, jstring text);

        // A null jstring is empty.
        static std::optional<std::string> ToOptional(JNIEnv* env, jstring text);

        static jstring ToJava(JNIEnv* env, const std::string& text);

        // Null for an empty optional.
        static jstring ToJava(JNIEnv* env, const std::optional<std::string>& text);

        // The exception in flight (inside a catch) thrown into Java when the native call returns:
        // a Java exception again, C#'s IOException family as java.io.IOException, the rest as
        // NativeException with C#'s type name.
        static void Rethrow(JNIEnv* env);

        // C#'s GetType().Name of a ported exception, e.g. "IOException".
        static std::string TypeName(const std::exception& error);

    private:
        static inline JavaVM* _vm = nullptr;
        static inline Classes _classes;

        static jclass Global(JNIEnv* env, const char* name);
    };

    // A Java exception thrown into native code: carried across the C++ frames (cleared, so they
    // can still call Java), thrown again when the native call returns to Java.
    class JavaException : public std::runtime_error
    {
    public:
        JavaException(JNIEnv* env, jthrowable error);

        jthrowable Error() const { return static_cast<jthrowable>(_error.get()); }

    private:
        std::shared_ptr<_jobject> _error;

        static std::string MessageOf(JNIEnv* env, jthrowable error);
    };

    // Runs a native method's body: an exception becomes a Java one and `failed` is returned.
    template <typename T, typename F>
    T Guard(JNIEnv* env, T failed, F&& body)
    {
        try
        {
            return body();
        }
        catch (...)
        {
            JavaBridge::Rethrow(env);
            return failed;
        }
    }

    // Guard for a void body.
    template <typename F>
    void Guard(JNIEnv* env, F&& body)
    {
        try
        {
            body();
        }
        catch (...)
        {
            JavaBridge::Rethrow(env);
        }
    }
}
