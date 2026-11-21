// The Java side's native methods (package io.github.nemo22.mdk.game): each one a domain call
// of Mdk.Formats or Mdk.Game, its exceptions thrown into Java (JavaBridge::Rethrow).
//
//   MdkData       at, imported, findIn, importFrom, copyFolder  ─► Formats::MdkData
//   StorageChoice folder, choose, forget, plan, copyUserFiles, isWritable ─► Flow::StorageChoice
//   TreePaths     resolve, primaryRoot                          ─► Flow::TreePaths
//   ModCatalog    folderName, folderIn                          ─► Mods::ModCatalog
//   HdGenerator   oldFolder, migrate                            ─► HdTextures::HdGenerator
//   Log           capture, error                                ─► Diagnostics::Log, Console
//   DotNetRandom  create, nextSingle, next, release             ─► Mdk::Random

#include <jni.h>

#include <functional>
#include <memory>
#include <string>

#include "Mdk.Android/JavaBridge.h"
#include "Mdk.Android/JavaTree.h"
#include "Mdk.Core/Console.h"
#include "Mdk.Core/DotNet.h"
#include "Mdk.Engine/Diagnostics/LogWriter.h"
#include "Mdk.Formats/MdkData.h"
#include "Mdk.Game/Flow/StorageChoice.h"
#include "Mdk.Game/Flow/TreePaths.h"
#include "Mdk.Game/HdTextures/HdGenerator.h"
#include "Mdk.Game/Mods/ModCatalog.h"

using Mdk::Android::Guard;
using Mdk::Android::JavaBridge;
using Mdk::Android::JavaTree;
using Mdk::Formats::MdkData;
using Mdk::Game::Flow::FolderAccess;
using Mdk::Game::Flow::StorageChoice;
using Mdk::Game::Flow::StoragePlan;
using Mdk::Game::Flow::TreePaths;

namespace
{
    // A Java MdkData holds the C++ installation: a heap std::shared_ptr's address.
    using DataHandle = std::shared_ptr<MdkData>;

    // A new Java MdkData for `data`, or null.
    jobject ToJava(JNIEnv* env, const std::shared_ptr<MdkData>& data)
    {
        if (data == nullptr)
        {
            return nullptr;
        }

        auto type = JavaBridge::Types().MdkData;
        auto make = env->GetMethodID(type, "<init>", "(JLjava/lang/String;)V");
        auto dir = JavaBridge::ToJava(env, data->Dir());
        auto* handle = new DataHandle(data);
        auto result = env->NewObject(type, make, reinterpret_cast<jlong>(handle), dir);
        env->DeleteLocalRef(dir);
        if (result == nullptr)
        {
            delete handle;
        }

        JavaBridge::Check(env);
        return result;
    }

    // The C++ installation of a Java MdkData, or null.
    std::shared_ptr<MdkData> FromJava(JNIEnv* env, jobject data)
    {
        if (data == nullptr)
        {
            return nullptr;
        }

        auto field = env->GetFieldID(JavaBridge::Types().MdkData, "handle", "J");
        return *reinterpret_cast<DataHandle*>(env->GetLongField(data, field));
    }

    // A Java Progress as MdkData's (files done, files) callback.
    std::function<void(int, int)> ProgressOf(JNIEnv* env, jobject progress)
    {
        if (progress == nullptr)
        {
            return nullptr;
        }

        auto update = env->GetMethodID(JavaBridge::Types().Progress, "update", "(II)V");
        return [env, progress, update](int done, int total)
        {
            env->CallVoidMethod(progress, update, done, total);
            JavaBridge::Check(env);
        };
    }

    std::string Text(JNIEnv* env, jstring text)
    {
        return JavaBridge::ToString(env, text);
    }
}

extern "C"
{
#ifdef MDK_STATIC_SDL
    // SDL's JNI_OnLoad, renamed by CMakeLists: SDL is linked in, so this library loads it too.
    jint SDL_JNI_OnLoad(JavaVM* vm, void* reserved);
#endif

    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
    {
#ifdef MDK_STATIC_SDL
        SDL_JNI_OnLoad(vm, reserved);
#endif
        return JavaBridge::Load(vm) ? JNI_VERSION_1_6 : JNI_ERR;
    }

    // ---- MdkData ----

    JNIEXPORT jobject JNICALL Java_io_github_nemo22_mdk_game_MdkData_at(JNIEnv* env, jclass, jstring folder)
    {
        return Guard(env, jobject{}, [&] { return ToJava(env, MdkData::At(Text(env, folder))); });
    }

    JNIEXPORT jobject JNICALL Java_io_github_nemo22_mdk_game_MdkData_imported(JNIEnv* env, jclass, jstring dir)
    {
        return Guard(env, jobject{}, [&] { return ToJava(env, MdkData::Imported(Text(env, dir))); });
    }

    JNIEXPORT jstring JNICALL Java_io_github_nemo22_mdk_game_MdkData_findIn(JNIEnv* env, jclass, jobject tree)
    {
        return Guard(env, jstring{}, [&]
        {
            JavaTree data(env, tree);
            return JavaBridge::ToJava(env, MdkData::FindIn(data));
        });
    }

    JNIEXPORT jobject JNICALL Java_io_github_nemo22_mdk_game_MdkData_importFrom(JNIEnv* env, jclass, jobject tree,
        jstring root, jstring dir, jobject progress)
    {
        return Guard(env, jobject{}, [&]
        {
            JavaTree data(env, tree);
            return ToJava(env, MdkData::Import(data, Text(env, root), Text(env, dir), ProgressOf(env, progress)));
        });
    }

    JNIEXPORT jint JNICALL Java_io_github_nemo22_mdk_game_MdkData_copyFolder(JNIEnv* env, jclass, jobject tree,
        jstring root, jstring name, jstring target, jobject progress)
    {
        return Guard(env, jint{}, [&]
        {
            JavaTree data(env, tree);
            return static_cast<jint>(MdkData::CopyFolder(data, Text(env, root), Text(env, name), Text(env, target),
                ProgressOf(env, progress)));
        });
    }

    JNIEXPORT void JNICALL Java_io_github_nemo22_mdk_game_MdkData_release(JNIEnv*, jclass, jlong handle)
    {
        delete reinterpret_cast<DataHandle*>(handle);
    }

    // ---- StorageChoice ----

    JNIEXPORT jstring JNICALL Java_io_github_nemo22_mdk_game_StorageChoice_folder(JNIEnv* env, jclass, jstring appFolder)
    {
        return Guard(env, jstring{}, [&] { return JavaBridge::ToJava(env, StorageChoice(Text(env, appFolder)).Folder()); });
    }

    JNIEXPORT void JNICALL Java_io_github_nemo22_mdk_game_StorageChoice_choose(JNIEnv* env, jclass, jstring appFolder,
        jstring folder)
    {
        Guard(env, [&] { StorageChoice(Text(env, appFolder)).Choose(Text(env, folder)); });
    }

    JNIEXPORT void JNICALL Java_io_github_nemo22_mdk_game_StorageChoice_forget(JNIEnv* env, jclass, jstring appFolder)
    {
        Guard(env, [&] { StorageChoice(Text(env, appFolder)).Forget(); });
    }

    JNIEXPORT jobject JNICALL Java_io_github_nemo22_mdk_game_StorageChoice_plan(JNIEnv* env, jclass, jstring appFolder,
        jint access, jobject copy)
    {
        return Guard(env, jobject{}, [&]
        {
            StorageChoice choice(Text(env, appFolder));
            auto plan = choice.Plan(static_cast<FolderAccess>(access), FromJava(env, copy));
            auto data = ToJava(env, plan.Data);
            auto type = JavaBridge::Types().StoragePlan;
            auto make = env->GetMethodID(type, "<init>", "(ILio/github/nemo22/mdk/game/MdkData;)V");
            auto result = env->NewObject(type, make, static_cast<jint>(plan.Kind), data);
            JavaBridge::Check(env);
            return result;
        });
    }

    JNIEXPORT void JNICALL Java_io_github_nemo22_mdk_game_StorageChoice_copyUserFiles(JNIEnv* env, jclass, jstring from,
        jstring to)
    {
        Guard(env, [&] { StorageChoice::CopyUserFiles(Text(env, from), Text(env, to)); });
    }

    JNIEXPORT jboolean JNICALL Java_io_github_nemo22_mdk_game_StorageChoice_isWritable(JNIEnv* env, jclass, jstring folder)
    {
        return Guard(env, jboolean{JNI_FALSE}, [&]
        {
            return StorageChoice::IsWritable(Text(env, folder)) ? jboolean{JNI_TRUE} : jboolean{JNI_FALSE};
        });
    }

    // ---- TreePaths ----

    JNIEXPORT jstring JNICALL Java_io_github_nemo22_mdk_game_TreePaths_resolve(JNIEnv* env, jclass, jstring authority,
        jstring documentId, jstring primaryRoot)
    {
        return Guard(env, jstring{}, [&]
        {
            return JavaBridge::ToJava(env, TreePaths::Resolve(Text(env, authority), Text(env, documentId), Text(env, primaryRoot)));
        });
    }

    JNIEXPORT jstring JNICALL Java_io_github_nemo22_mdk_game_TreePaths_primaryRoot(JNIEnv* env, jclass, jstring appFiles)
    {
        return Guard(env, jstring{}, [&] { return JavaBridge::ToJava(env, TreePaths::PrimaryRoot(Text(env, appFiles))); });
    }

    // ---- ModCatalog ----

    JNIEXPORT jstring JNICALL Java_io_github_nemo22_mdk_game_ModCatalog_folderName(JNIEnv* env, jclass)
    {
        return Guard(env, jstring{}, [&] { return JavaBridge::ToJava(env, std::string(Mdk::Game::Mods::ModCatalog::FolderName)); });
    }

    JNIEXPORT jstring JNICALL Java_io_github_nemo22_mdk_game_ModCatalog_folderIn(JNIEnv* env, jclass, jstring userFolder)
    {
        return Guard(env, jstring{}, [&] { return JavaBridge::ToJava(env, Mdk::Game::Mods::ModCatalog::FolderIn(Text(env, userFolder))); });
    }

    // ---- HdGenerator ----

    JNIEXPORT jstring JNICALL Java_io_github_nemo22_mdk_game_HdGenerator_oldFolder(JNIEnv* env, jclass)
    {
        return Guard(env, jstring{}, [&] { return JavaBridge::ToJava(env, std::string(Mdk::Game::HdTextures::HdGenerator::OldFolder)); });
    }

    JNIEXPORT jint JNICALL Java_io_github_nemo22_mdk_game_HdGenerator_migrate(JNIEnv* env, jclass, jstring userFolder)
    {
        return Guard(env, jint{}, [&] { return static_cast<jint>(Mdk::Game::HdTextures::HdGenerator::Migrate(Text(env, userFolder))); });
    }

    // ---- Log ----

    JNIEXPORT void JNICALL Java_io_github_nemo22_mdk_game_Log_capture(JNIEnv* env, jclass)
    {
        Guard(env, [] { Mdk::Engine::Diagnostics::Log::Capture(Mdk::Engine::Diagnostics::Log::Lines); });
    }

    JNIEXPORT void JNICALL Java_io_github_nemo22_mdk_game_Log_error(JNIEnv* env, jclass, jstring line)
    {
        Guard(env, [&] { Mdk::Console::Error().WriteLine(Text(env, line)); });
    }

    // ---- DotNetRandom ----

    JNIEXPORT jlong JNICALL Java_io_github_nemo22_mdk_game_DotNetRandom_create(JNIEnv* env, jclass, jint seed)
    {
        return Guard(env, jlong{}, [&] { return reinterpret_cast<jlong>(new Mdk::Random(seed)); });
    }

    JNIEXPORT jfloat JNICALL Java_io_github_nemo22_mdk_game_DotNetRandom_nextSingle(JNIEnv*, jclass, jlong handle)
    {
        return reinterpret_cast<Mdk::Random*>(handle)->NextSingle();
    }

    JNIEXPORT jint JNICALL Java_io_github_nemo22_mdk_game_DotNetRandom_next(JNIEnv*, jclass, jlong handle, jint maxValue)
    {
        return reinterpret_cast<Mdk::Random*>(handle)->Next(maxValue);
    }

    JNIEXPORT void JNICALL Java_io_github_nemo22_mdk_game_DotNetRandom_release(JNIEnv*, jclass, jlong handle)
    {
        delete reinterpret_cast<Mdk::Random*>(handle);
    }
}
