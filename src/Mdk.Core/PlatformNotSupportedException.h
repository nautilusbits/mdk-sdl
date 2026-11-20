#pragma once

// System.PlatformNotSupportedException (a NotSupportedException).

#include "Mdk.Core/DotNet.h"

namespace Mdk
{
    struct PlatformNotSupportedException : NotSupportedException { using NotSupportedException::NotSupportedException; };
}
