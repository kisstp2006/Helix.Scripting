#pragma once

#include <Helix/Scripting/Host/DotnetHost.h>

#include <bit>

// The runtime and the test assembly for the host tests and benchmarks (desktop builds with the .NET SDK, which pass
// their paths); a process starts the runtime once.
namespace helix::scripting::testing
{
#if defined(HX_TEST_RUNTIME_ASSEMBLY) && defined(HX_TEST_HOST_ASSEMBLY)
    inline constexpr bool kHostAvailable = true;
    inline constexpr const char* kRuntimeAssembly = HX_TEST_RUNTIME_ASSEMBLY;
    inline constexpr const char* kHostAssembly = HX_TEST_HOST_ASSEMBLY;
#else
    inline constexpr bool kHostAvailable = false;
    inline constexpr const char* kRuntimeAssembly = "";
    inline constexpr const char* kHostAssembly = "";
#endif

    inline bool StartHost(String& error)
    {
        DotnetHostDesc desc;
        desc.runtimeAssembly = kRuntimeAssembly;
        return DotnetHost::Start(desc, error);
    }

    inline AssemblyHandle HostTests(String& error)
    {
        if (!StartHost(error))
            return AssemblyHandle{};
        return DotnetHost::LoadAssembly(kHostAssembly, error);
    }

    template <class F>
    F ProbeFunction(AssemblyHandle assembly, StringView method, String& error)
    {
        return std::bit_cast<F>(DotnetHost::GetFunction(assembly, "Helix.Scripting.HostTests.Probe", method, error));
    }
}
