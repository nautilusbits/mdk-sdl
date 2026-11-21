#pragma once

// Directory.CreateTempSubdirectory(prefix), deleted with its contents at the end of the scope
// (the xUnit tests' IDisposable fixture).

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace Mdk::Tests
{
    class TempDir
    {
    public:
        explicit TempDir(const std::string& prefix)
        {
            std::random_device device;
            std::mt19937 random(device());
            auto base = std::filesystem::temp_directory_path();
            std::filesystem::path dir;
            do
            {
                dir = base / (prefix + std::to_string(random()));
            } while (std::filesystem::exists(dir));

            std::filesystem::create_directories(dir);
            auto text = dir.generic_u8string();
            _path.assign(text.begin(), text.end());
        }

        ~TempDir()
        {
            std::error_code error;
            std::filesystem::remove_all(std::filesystem::path(std::u8string(_path.begin(), _path.end())), error);
        }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

        const std::string& Path() const { return _path; }

    private:
        std::string _path;
    };
}
