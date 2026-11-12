#pragma once

#include <Helix/Core/Basics/Platform.h>
#include <Helix/Core/Basics/Types.h>

// The native hosting interface of .NET (hostfxr), declared from its documentation: the calls Helix makes and the
// values it passes. Strings are UTF-16 on Windows and UTF-8 elsewhere.
namespace helix::scripting::hostfxr
{
#if HX_PLATFORM_WINDOWS
    using Char = wchar_t;
#else
    using Char = char;
#endif

    using Handle = void*;

    struct InitializeParameters
    {
        usize size = sizeof(InitializeParameters);
        // The executable that hosts the runtime.
        const Char* hostPath = nullptr;
        // The folder with host/fxr and shared/ (the runtime that is used).
        const Char* dotnetRoot = nullptr;
    };

    // The runtime delegates Helix asks for.
    enum DelegateType : i32
    {
        GetFunctionPointer = 6,
        LoadAssembly = 7,
    };

    using InitializeForRuntimeConfigFn = i32 (*)(const Char* runtimeConfigPath, const InitializeParameters* parameters, Handle* handle);
    using GetRuntimeDelegateFn = i32 (*)(Handle handle, i32 type, void** function);
    using CloseFn = i32 (*)(Handle handle);
    using ErrorWriterFn = void (*)(const Char* message);
    using SetErrorWriterFn = ErrorWriterFn (*)(ErrorWriterFn writer);

    // Loads an assembly into the default load context.
    using LoadAssemblyFn = i32 (*)(const Char* assemblyPath, void* loadContext, void* reserved);
    // The function pointer of a static method; with kUnmanagedCallersOnly as the delegate type, of a method marked
    // UnmanagedCallersOnly. The type is "Namespace.Type, Assembly".
    using GetFunctionPointerFn = i32 (*)(const Char* typeName, const Char* methodName, const Char* delegateTypeName, void* loadContext,
                                         void* reserved, void** function);

    inline const Char* UnmanagedCallersOnly() { return reinterpret_cast<const Char*>(~uptr(0)); }

#if HX_PLATFORM_WINDOWS
    inline constexpr const char* kLibraryName = "hostfxr.dll";
#else
    inline constexpr const char* kLibraryName = "libhostfxr.so";
#endif
}
