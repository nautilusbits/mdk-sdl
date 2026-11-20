#include "Mdk.Android/SetupActivity.h"

#include "Mdk.Android/JavaBridge.h"

namespace Mdk::Android
{
    namespace
    {
        // SetupActivity's static String method `name`, waited for.
        void Await(const char* name, const std::optional<std::string>& value)
        {
            auto* env = JavaBridge::Env();
            auto type = JavaBridge::Types().SetupActivity;
            auto method = env->GetStaticMethodID(type, name, "(Ljava/lang/String;)V");
            auto text = JavaBridge::ToJava(env, value);
            env->CallStaticVoidMethod(type, method, text);
            if (text != nullptr)
            {
                env->DeleteLocalRef(text);
            }

            JavaBridge::Check(env);
        }
    }

    void SetupActivity::Reimport(const std::optional<std::string>& inPlace)
    {
        Await("reimport", inPlace);
    }

    void SetupActivity::Stopped(const std::string& message)
    {
        Await("stopped", message);
    }
}
