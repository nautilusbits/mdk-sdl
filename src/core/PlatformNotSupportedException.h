#pragma once

// System.PlatformNotSupportedException (a NotSupportedException).

#include "core/DotNet.h"

namespace Mdk
{
    struct PlatformNotSupportedException : NotSupportedException { using NotSupportedException::NotSupportedException; };
}
