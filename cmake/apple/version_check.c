// clang's @available calls compiler-rt's __isPlatformVersionAtLeast; this toolchain has no
// Darwin compiler-rt, and SDL's Objective-C uses @available. Same lookup as compiler-rt: dyld's
// _availability_version_check, else the kernel's product version ("13.3.1").
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>

typedef struct { uint32_t platform; uint32_t version; } VersionCheck;
typedef bool (*CheckFn)(uint32_t count, VersionCheck versions[]);

int32_t __isPlatformVersionAtLeast(uint32_t platform, uint32_t major, uint32_t minor, uint32_t subminor)
{
    // Packed like dyld's versions: 0xMMMMmmss.
    static CheckFn check;
    if (!check) {
        check = (CheckFn)dlsym(RTLD_DEFAULT, "_availability_version_check");
    }
    if (check) {
        VersionCheck wanted = { platform, (major & 0xffff) << 16 | (minor & 0xff) << 8 | (subminor & 0xff) };
        return check(1, &wanted);
    }

    char text[32] = { 0 };
    size_t size = sizeof(text) - 1;
    uint32_t os[3] = { 0, 0, 0 };
    if (sysctlbyname("kern.osproductversion", text, &size, NULL, 0) != 0) {
        return 0;
    }
    sscanf(text, "%u.%u.%u", &os[0], &os[1], &os[2]);
    if (os[0] != major) {
        return os[0] > major;
    }
    if (os[1] != minor) {
        return os[1] > minor;
    }
    return os[2] >= subminor;
}
