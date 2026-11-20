#include "Mdk.Android/JavaBridge.h"

#include <filesystem>
#include <new>
#include <vector>

#include "Mdk.Core/DotNet.h"

namespace Mdk::Android
{
    namespace
    {
        constexpr jint Version = JNI_VERSION_1_6;

        // UTF-8 lead bytes and the surrogates UTF-16 spells the rest of Unicode with.
        constexpr char32_t Replacement = 0xFFFD;
        constexpr char32_t LastBmp = 0xFFFF;
        constexpr char32_t SupplementaryBase = 0x10000;
        constexpr char16_t HighSurrogate = 0xD800;
        constexpr char16_t LowSurrogate = 0xDC00;
        constexpr char16_t SurrogateEnd = 0xE000;
        constexpr int SurrogateBits = 10;
        constexpr char32_t SurrogateMask = 0x3FF;

        // Java strings are UTF-16; JNI's own UTF-8 is "modified" (no 4-byte sequences).
        std::u16string Utf16(const std::string& text)
        {
            std::u16string result;
            result.reserve(text.size());
            for (size_t i = 0; i < text.size();)
            {
                auto lead = static_cast<unsigned char>(text[i]);
                auto length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 0;
                if (length == 0 || i + length > text.size())
                {
                    result.push_back(static_cast<char16_t>(Replacement));
                    i++;
                    continue;
                }

                char32_t code = length == 1 ? lead : lead & (0xFF >> (length + 1));
                for (int k = 1; k < length; k++)
                {
                    code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
                }

                i += length;
                if (code <= LastBmp)
                {
                    result.push_back(static_cast<char16_t>(code));
                    continue;
                }

                code -= SupplementaryBase;
                result.push_back(static_cast<char16_t>(HighSurrogate + (code >> SurrogateBits)));
                result.push_back(static_cast<char16_t>(LowSurrogate + (code & SurrogateMask)));
            }

            return result;
        }

        void AppendUtf8(std::string& result, char32_t code)
        {
            if (code < 0x80)
            {
                result.push_back(static_cast<char>(code));
                return;
            }

            if (code < 0x800)
            {
                result.push_back(static_cast<char>(0xC0 | (code >> 6)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                return;
            }

            if (code < SupplementaryBase)
            {
                result.push_back(static_cast<char>(0xE0 | (code >> 12)));
                result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                return;
            }

            result.push_back(static_cast<char>(0xF0 | (code >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }

        std::string Utf8(const jchar* chars, jsize length)
        {
            std::string result;
            result.reserve(length);
            for (jsize i = 0; i < length; i++)
            {
                char32_t code = chars[i];
                auto high = code >= HighSurrogate && code < LowSurrogate;
                if (high && i + 1 < length && chars[i + 1] >= LowSurrogate && chars[i + 1] < SurrogateEnd)
                {
                    code = SupplementaryBase + ((code - HighSurrogate) << SurrogateBits) + (chars[i + 1] - LowSurrogate);
                    i++;
                }

                AppendUtf8(result, code);
            }

            return result;
        }

        bool IsIo(const std::exception& error)
        {
            return dynamic_cast<const IOException*>(&error) != nullptr
                || dynamic_cast<const FileNotFoundException*>(&error) != nullptr
                || dynamic_cast<const DirectoryNotFoundException*>(&error) != nullptr
                || dynamic_cast<const std::filesystem::filesystem_error*>(&error) != nullptr;
        }
    }

    bool JavaBridge::Load(JavaVM* vm)
    {
        _vm = vm;
        auto* env = Env();
        _classes.DataTree = Global(env, "io/github/nemo22/mdk/game/DataTree");
        _classes.DataEntry = Global(env, "io/github/nemo22/mdk/game/DataEntry");
        _classes.Progress = Global(env, "io/github/nemo22/mdk/game/Progress");
        _classes.InputStream = Global(env, "java/io/InputStream");
        _classes.IOException = Global(env, "java/io/IOException");
        _classes.NativeException = Global(env, "io/github/nemo22/mdk/game/NativeException");
        _classes.SetupActivity = Global(env, "io/github/nemo22/mdk/SetupActivity");
        _classes.MdkData = Global(env, "io/github/nemo22/mdk/game/MdkData");
        _classes.StoragePlan = Global(env, "io/github/nemo22/mdk/game/StoragePlan");
        return _classes.DataTree != nullptr && _classes.DataEntry != nullptr && _classes.Progress != nullptr
            && _classes.InputStream != nullptr && _classes.IOException != nullptr
            && _classes.NativeException != nullptr && _classes.SetupActivity != nullptr
            && _classes.MdkData != nullptr && _classes.StoragePlan != nullptr;
    }

    jclass JavaBridge::Global(JNIEnv* env, const char* name)
    {
        auto local = env->FindClass(name);
        if (local == nullptr)
        {
            env->ExceptionClear();
            return nullptr;
        }

        auto global = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);
        return global;
    }

    JNIEnv* JavaBridge::Env()
    {
        JNIEnv* env = nullptr;
        if (_vm->GetEnv(reinterpret_cast<void**>(&env), Version) == JNI_OK)
        {
            return env;
        }

        _vm->AttachCurrentThread(&env, nullptr);
        return env;
    }

    void JavaBridge::Check(JNIEnv* env)
    {
        auto error = env->ExceptionOccurred();
        if (error == nullptr)
        {
            return;
        }

        env->ExceptionClear();
        JavaException thrown(env, error);
        env->DeleteLocalRef(error);
        throw thrown;
    }

    std::string JavaBridge::ToString(JNIEnv* env, jstring text)
    {
        if (text == nullptr)
        {
            return {};
        }

        auto length = env->GetStringLength(text);
        auto* chars = env->GetStringChars(text, nullptr);
        auto result = Utf8(chars, length);
        env->ReleaseStringChars(text, chars);
        return result;
    }

    std::optional<std::string> JavaBridge::ToOptional(JNIEnv* env, jstring text)
    {
        if (text == nullptr)
        {
            return std::nullopt;
        }

        return ToString(env, text);
    }

    jstring JavaBridge::ToJava(JNIEnv* env, const std::string& text)
    {
        auto wide = Utf16(text);
        auto result = env->NewString(reinterpret_cast<const jchar*>(wide.data()), static_cast<jsize>(wide.size()));
        Check(env);
        return result;
    }

    jstring JavaBridge::ToJava(JNIEnv* env, const std::optional<std::string>& text)
    {
        return text.has_value() ? ToJava(env, *text) : nullptr;
    }

    void JavaBridge::Rethrow(JNIEnv* env)
    {
        try
        {
            throw;
        }
        catch (const JavaException& e)
        {
            env->Throw(e.Error());
        }
        catch (const std::exception& e)
        {
            if (IsIo(e))
            {
                env->ThrowNew(_classes.IOException, e.what());
                return;
            }

            ThrowNative(env, TypeName(e), e.what());
        }
        catch (...)
        {
            ThrowNative(env, "Exception", "unknown");
        }
    }

    void JavaBridge::ThrowNative(JNIEnv* env, const std::string& typeName, const std::string& message)
    {
        auto make = env->GetMethodID(_classes.NativeException, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V");
        auto type = ToJava(env, typeName);
        auto text = ToJava(env, message);
        auto error = static_cast<jthrowable>(env->NewObject(_classes.NativeException, make, type, text));
        env->Throw(error);
    }

    std::string JavaBridge::TypeName(const std::exception& error)
    {
        if (dynamic_cast<const JavaException*>(&error) != nullptr)
        {
            return "JavaException";
        }

        if (dynamic_cast<const FileNotFoundException*>(&error) != nullptr)
        {
            return "FileNotFoundException";
        }

        if (dynamic_cast<const DirectoryNotFoundException*>(&error) != nullptr)
        {
            return "DirectoryNotFoundException";
        }

        if (IsIo(error))
        {
            return "IOException";
        }

        if (dynamic_cast<const InvalidDataException*>(&error) != nullptr)
        {
            return "InvalidDataException";
        }

        if (dynamic_cast<const ArgumentException*>(&error) != nullptr)
        {
            return "ArgumentException";
        }

        if (dynamic_cast<const InvalidOperationException*>(&error) != nullptr)
        {
            return "InvalidOperationException";
        }

        if (dynamic_cast<const NotSupportedException*>(&error) != nullptr)
        {
            return "NotSupportedException";
        }

        if (dynamic_cast<const FormatException*>(&error) != nullptr)
        {
            return "FormatException";
        }

        if (dynamic_cast<const OperationCanceledException*>(&error) != nullptr)
        {
            return "OperationCanceledException";
        }

        if (dynamic_cast<const std::bad_alloc*>(&error) != nullptr)
        {
            return "OutOfMemoryException";
        }

        return "Exception";
    }

    JavaException::JavaException(JNIEnv* env, jthrowable error)
        : std::runtime_error(MessageOf(env, error)),
          _error(env->NewGlobalRef(error), [](jobject held) { JavaBridge::Env()->DeleteGlobalRef(held); })
    {
    }

    std::string JavaException::MessageOf(JNIEnv* env, jthrowable error)
    {
        auto type = env->GetObjectClass(error);
        auto describe = env->GetMethodID(type, "toString", "()Ljava/lang/String;");
        auto text = static_cast<jstring>(env->CallObjectMethod(error, describe));
        env->DeleteLocalRef(type);
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return "Java exception";
        }

        auto message = JavaBridge::ToString(env, text);
        env->DeleteLocalRef(text);
        return message;
    }
}
