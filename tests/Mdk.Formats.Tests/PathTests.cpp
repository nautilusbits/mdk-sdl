#include <doctest.h>

#include "Mdk.Core/Files.h"
#include "Mdk.Formats/CaseInsensitivePath.h"
#include "Shared/TempDir.h"

using namespace Mdk;
using namespace Mdk::Formats;

// Data paths match the files whatever their case: the game asks for MISC/MDKFONT.FTI,
// the installation has MISC/mdkfont.fti (case-sensitive file systems).

TEST_CASE("PathTests.PartsMatchWhateverTheirCase")
{
    Mdk::Tests::TempDir temp("mdk-paths");
    auto& dir = temp.Path();
    auto folder = Path::Combine(dir, "Misc", "FLIC");
    Directory::CreateDirectory(folder);
    File::WriteAllText(Path::Combine(dir, "Misc", "mdkfont.fti"), "");
    File::WriteAllText(Path::Combine(folder, "Mdk12.flc"), "");

    CHECK(Path::Combine(dir, "Misc", "mdkfont.fti") == CaseInsensitivePath::Resolve(dir, "MISC/MDKFONT.FTI"));
    CHECK(Path::Combine(folder, "Mdk12.flc") == CaseInsensitivePath::Resolve(dir, "MISC/FLIC/MDK12.FLC"));
}

TEST_CASE("PathTests.MissingFilesKeepTheirName")
{
    Mdk::Tests::TempDir temp("mdk-paths");
    auto& dir = temp.Path();
    CHECK(Path::Combine(dir, "MISC", "NONE.GIF") == CaseInsensitivePath::Resolve(dir, "MISC/NONE.GIF"));
}
