#pragma once

#include <Helix/Core/Logging/Log.h>
#include <Helix/Scripting/Host/DotnetHost.h>

// What the runtime starters share: the log channel, the error of the call in progress, version order, the shared
// framework's folder, and the built in starters of the platform.
namespace helix::scripting::detail
{
    extern LogChannel gHostLog;

    // Where the error of the call in progress goes (the managed side's SetError, hostfxr's error writer).
    String*& CallError();

    bool Fail(String& error, String text);

    // "11.0.0-rc.3.26522.148" against another: the numbers, then a release over a prerelease, then the prerelease
    // parts (numbers by value); -1, 0 or 1.
    i32 CompareVersions(StringView a, StringView b);

    // The highest version folder of <root>/shared/<framework> with the major version of `version` and not below it
    // (any version when `version` is empty); empty when there is none.
    String FindFramework(StringView root, StringView framework, StringView version);

    // The managed side's type and method, as the starters look them up.
    inline constexpr StringView kManagedAssembly = "Helix.Scripting.Runtime";
    inline constexpr StringView kManagedType = "Helix.Scripting.Host";
    inline constexpr StringView kManagedMethod = "Initialize";

    // The platform's starters, in order of preference (the first is the default).
    Span<const DotnetRuntimeStarter> BuiltinRuntimes();

    extern const DotnetRuntimeStarter kHostfxrRuntime;
    extern const DotnetRuntimeStarter kCoreclrRuntime;
    extern const DotnetRuntimeStarter kBrowserRuntime;
}
