#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>

// The .NET host: a runtime started by one of the registered runtime starters, the managed side of Helix
// (Helix.Scripting.Runtime) in it, and the engine's load context into which engine and script assemblies are loaded.
// The starters: "hostfxr" (Windows and Linux: the installed runtime through hostfxr, the default there), "coreclr"
// (the coreclr library with a framework folder: Android's default, and on the desktop for tests) and "browser" (the
// Web: the runtime of the app's module, started before native code runs). Calls between the languages go through
// function pointers of UnmanagedCallersOnly methods; objects of C# that native code keeps are GC handles.
namespace helix::scripting
{
    struct DotnetHostDesc
    {
        // The managed side: Helix.Scripting.Runtime.dll (for hostfxr its runtimeconfig.json next to it).
        StringView runtimeAssembly;
        // The runtime's folder (with host/fxr and shared/); empty: searched (see FindDotnetRoots).
        StringView dotnetRoot;
        // The starter by name; empty: HELIX_DOTNET_RUNTIME of the environment, else the platform's default.
        StringView runtime;
        // coreclr: the folder of the coreclr library and the runtime's native libraries, and the folder of the
        // framework's assemblies (both empty: the shared framework of the runtime found as for hostfxr).
        StringView nativeFolder;
        StringView frameworkFolder;
    };

    // The folders a runtime is looked for in, in order: `dotnet/` next to the executable, DOTNET_ROOT,
    // HELIX_DOTNET_ROOT, the default install folders.
    void FindDotnetRoots(Array<String>& out);

    // The managed side's Initialize (Helix.Scripting.Host.Initialize, UnmanagedCallersOnly).
    using ManagedInitializeFn = i32 (*)(void* native, void* managed);

    // A started runtime: the managed side's Initialize and the folder the runtime came from.
    struct DotnetRuntime
    {
        ManagedInitializeFn initialize = nullptr;
        String root;
    };

    // Starts a runtime (once per process) and finds the managed side's Initialize; false with the error.
    struct DotnetRuntimeStarter
    {
        StringView name;
        bool (*start)(const DotnetHostDesc& desc, DotnetRuntime& out, String& error) = nullptr;
    };

    // Adds a starter (a later one of the same name replaces it); the built in ones are there from the start.
    void RegisterDotnetRuntime(const DotnetRuntimeStarter& starter);
    // The starter of a name; null when there is none.
    const DotnetRuntimeStarter* FindDotnetRuntime(StringView name);
    // The platform's default starter's name.
    StringView DefaultDotnetRuntime();

    // A managed assembly of the engine's load context (0 is none).
    struct AssemblyHandle
    {
        u64 value = 0;
        bool IsValid() const { return value != 0; }
    };

    class DotnetHost
    {
    public:
        // Starts the runtime (once per process) and the managed side; again after Stop, the managed side only.
        static bool Start(const DotnetHostDesc& desc, String& error);
        // The managed side lets go of what it holds (its load contexts, handles); the runtime stays until the process
        // ends (it cannot be unloaded).
        static void Stop();
        static bool IsRunning();
        // The runtime's folder in use, and the starter's name.
        static StringView DotnetRoot();
        static StringView Runtime();

        // Loads an assembly into the engine's load context (its folder joins the folders the context resolves from);
        // an assembly the app already holds (the Web's) by its name.
        static AssemblyHandle LoadAssembly(StringView path, String& error);
        // The function pointer of a static method marked UnmanagedCallersOnly ("Namespace.Type", "Method").
        static void* GetFunction(AssemblyHandle assembly, StringView type, StringView method, String& error);

        // GC handles native code holds: another handle of the same object, its release, a weak handle of it, whether
        // the object is still there.
        static u64 RetainHandle(u64 handle);
        static void ReleaseHandle(u64 handle);
        static u64 WeakHandle(u64 handle);
        static bool IsHandleAlive(u64 handle);
        // A full collection, finalizers run.
        static void CollectGarbage();
    };
}
