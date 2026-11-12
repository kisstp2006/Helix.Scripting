#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>

// The .NET host of the desktop (Windows, Linux): the runtime started through hostfxr, the managed side of Helix
// (Helix.Scripting.Runtime) loaded into it, and the engine's load context into which engine and script assemblies
// are loaded. Calls between the languages go through function pointers of UnmanagedCallersOnly methods; objects
// of C# that native code keeps are GC handles. Elsewhere Start answers that this host is not there.
namespace helix::scripting
{
    struct DotnetHostDesc
    {
        // The managed side: Helix.Scripting.Runtime.dll (its runtimeconfig.json next to it).
        StringView runtimeAssembly;
        // The runtime's folder (with host/fxr and shared/); empty: searched (see FindDotnetRoots).
        StringView dotnetRoot;
    };

    // The folders a runtime is looked for in, in order: `dotnet/` next to the executable, DOTNET_ROOT,
    // HELIX_DOTNET_ROOT, the default install folders.
    void FindDotnetRoots(Array<String>& out);

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
        // The runtime's folder and version in use.
        static StringView DotnetRoot();

        // Loads an assembly into the engine's load context (its folder joins the folders the context resolves from).
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
