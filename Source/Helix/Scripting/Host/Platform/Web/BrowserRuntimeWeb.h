#pragma once

#include <Helix/Scripting/Host/DotnetHost.h>

namespace helix::scripting::detail
{
    // The managed side's Initialize the app's C# entry hands over.
    void SetBrowserInitialize(ManagedInitializeFn initialize);
}
