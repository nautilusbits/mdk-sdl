#include "Mdk.Android/JavaTree.h"

#include <istream>
#include <streambuf>

#include "Mdk.Android/JavaBridge.h"

namespace Mdk::Android
{
    namespace
    {
        // Bytes read from Java at a time.
        constexpr jint ReadSize = 81920;
        // InputStream.read's end.
        constexpr jint EndOfStream = -1;

        // A java.io.InputStream as a C++ stream buffer: its failures pass on as JavaException.
        class InputBuffer final : public std::streambuf
        {
        public:
            InputBuffer(JNIEnv* env, jobject stream)
                : _env(env), _stream(env->NewGlobalRef(stream)), _bytes(nullptr), _buffer(ReadSize)
            {
                auto type = JavaBridge::Types().InputStream;
                _read = env->GetMethodID(type, "read", "([B)I");
                _close = env->GetMethodID(type, "close", "()V");
                auto bytes = env->NewByteArray(ReadSize);
                JavaBridge::Check(env);
                _bytes = static_cast<jbyteArray>(env->NewGlobalRef(bytes));
                env->DeleteLocalRef(bytes);
            }

            ~InputBuffer() override
            {
                // using's Dispose: a failing close can't throw from here.
                _env->CallVoidMethod(_stream, _close);
                if (_env->ExceptionCheck())
                {
                    _env->ExceptionClear();
                }

                _env->DeleteGlobalRef(_bytes);
                _env->DeleteGlobalRef(_stream);
            }

            InputBuffer(const InputBuffer&) = delete;
            InputBuffer& operator=(const InputBuffer&) = delete;

        protected:
            int_type underflow() override
            {
                if (gptr() < egptr())
                {
                    return traits_type::to_int_type(*gptr());
                }

                // read may return 0 bytes only for an empty array: ask again until data or the end.
                jint read = 0;
                while (read == 0)
                {
                    read = _env->CallIntMethod(_stream, _read, _bytes);
                    JavaBridge::Check(_env);
                }

                if (read == EndOfStream)
                {
                    return traits_type::eof();
                }

                _env->GetByteArrayRegion(_bytes, 0, read, reinterpret_cast<jbyte*>(_buffer.data()));
                setg(_buffer.data(), _buffer.data(), _buffer.data() + read);
                return traits_type::to_int_type(*gptr());
            }

        private:
            JNIEnv* _env;
            jobject _stream;
            jbyteArray _bytes;
            jmethodID _read;
            jmethodID _close;
            std::vector<char> _buffer;
        };

        class InputStream final : public std::istream
        {
        public:
            InputStream(JNIEnv* env, jobject stream) : std::istream(nullptr), _buffer(env, stream) { rdbuf(&_buffer); }

        private:
            InputBuffer _buffer;
        };
    }

    JavaTree::JavaTree(JNIEnv* env, jobject tree) : _env(env), _tree(tree)
    {
        const auto& types = JavaBridge::Types();
        _list = env->GetMethodID(types.DataTree, "list", "(Ljava/lang/String;)[Lio/github/nemo22/mdk/game/DataEntry;");
        _open = env->GetMethodID(types.DataTree, "open", "(Ljava/lang/String;)Ljava/io/InputStream;");
        _name = env->GetFieldID(types.DataEntry, "name", "Ljava/lang/String;");
        _kind = env->GetFieldID(types.DataEntry, "kind", "I");
        _size = env->GetFieldID(types.DataEntry, "size", "J");
        JavaBridge::Check(env);
    }

    std::vector<Formats::DataEntry> JavaTree::List(const std::string& folder)
    {
        auto name = JavaBridge::ToJava(_env, folder);
        auto array = static_cast<jobjectArray>(_env->CallObjectMethod(_tree, _list, name));
        _env->DeleteLocalRef(name);
        JavaBridge::Check(_env);

        std::vector<Formats::DataEntry> entries;
        auto count = _env->GetArrayLength(array);
        for (jsize i = 0; i < count; i++)
        {
            auto item = _env->GetObjectArrayElement(array, i);
            auto text = static_cast<jstring>(_env->GetObjectField(item, _name));
            Formats::DataEntry entry;
            entry.Name = JavaBridge::ToString(_env, text);
            entry.Kind = static_cast<Formats::EntryKind>(_env->GetIntField(item, _kind));
            entry.Size = _env->GetLongField(item, _size);
            entries.push_back(entry);
            _env->DeleteLocalRef(text);
            _env->DeleteLocalRef(item);
        }

        _env->DeleteLocalRef(array);
        return entries;
    }

    std::unique_ptr<std::istream> JavaTree::Open(const std::string& file)
    {
        auto name = JavaBridge::ToJava(_env, file);
        auto stream = _env->CallObjectMethod(_tree, _open, name);
        _env->DeleteLocalRef(name);
        JavaBridge::Check(_env);

        auto result = std::make_unique<InputStream>(_env, stream);
        _env->DeleteLocalRef(stream);
        return result;
    }
}
