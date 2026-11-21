#pragma once

#include <jni.h>

#include <memory>
#include <string>
#include <vector>

#include "formats/MdkData.h"

namespace Mdk::Android
{
    // DocumentTree (Java: the folder the user picked, read through its content URIs) as the
    // IDataTree MdkData imports from. Calls Java on the calling thread: the setup screen's
    // worker, which is in a native call of its own.
    //   MdkData.Import ─List/Open─► JavaTree ─JNI─► DocumentTree ─► ContentResolver
    class JavaTree final : public Formats::IDataTree
    {
    public:
        // `tree` is a local reference of the native call; it outlives this.
        JavaTree(JNIEnv* env, jobject tree);

        std::vector<Formats::DataEntry> List(const std::string& folder) override;

        std::unique_ptr<std::istream> Open(const std::string& file) override;

    private:
        JNIEnv* _env;
        jobject _tree;
        jmethodID _list;
        jmethodID _open;
        jfieldID _name;
        jfieldID _kind;
        jfieldID _size;
    };
}
