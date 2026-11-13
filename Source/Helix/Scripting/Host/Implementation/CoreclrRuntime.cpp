#include <Helix/Core/Basics/Platform.h>

#if HX_PLATFORM_WINDOWS || HX_PLATFORM_LINUX || HX_PLATFORM_ANDROID

#include <Helix/Core/Strings/Format.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/System/DynamicLibrary.h>
#include <Helix/Platform/System/Process.h>
#include <Helix/Scripting/Host/Implementation/DotnetRuntimes.h>

#include <bit>

// The coreclr starter: the runtime library itself (coreclr_initialize, coreclr_create_delegate) with the trusted
// assemblies of a framework folder, as Android's activity hosts it; on the desktop the installed shared framework
// serves, so the same code runs in the tests.
namespace helix::scripting::detail
{
    namespace
    {
        using InitializeFn = i32 (*)(const char* exePath, const char* appDomainName, i32 propertyCount, const char** keys, const char** values,
                                     void** host, u32* domainId);
        using CreateDelegateFn = i32 (*)(void* host, u32 domainId, const char* assembly, const char* type, const char* method, void** function);

#if HX_PLATFORM_WINDOWS
        constexpr const char* kLibrary = "coreclr.dll";
        constexpr char kPathSeparator = ';';
#else
        constexpr const char* kLibrary = "libcoreclr.so";
        constexpr char kPathSeparator = ':';
#endif

        // The runtime takes Windows paths with backslashes only (forward ones make coreclr_initialize fail).
        String NativePath(String path)
        {
#if HX_PLATFORM_WINDOWS
            for (usize i = 0; i < path.Size(); i++)
                if (path[i] == '/')
                    path.Data()[i] = '\\';
#endif
            return path;
        }

        struct Lists
        {
            String trusted;
            bool haveRuntime = false;
        };

        // The assemblies of a folder as trusted platform assemblies.
        void AddAssemblies(StringView folder, Lists& lists)
        {
            struct Walk
            {
                StringView folder;
                Lists* lists;
            } walk{folder, &lists};
            platform::ListDirectory(
                folder,
                [](const platform::DirectoryEntry& e, void* user) {
                    auto* w = static_cast<Walk*>(user);
                    if (e.isDirectory || !e.name.EndsWith(".dll"))
                        return;
                    if (e.name == "Helix.Scripting.Runtime.dll")
                        w->lists->haveRuntime = true;
                    if (!w->lists->trusted.Empty())
                        w->lists->trusted.Append(StringView(&kPathSeparator, 1));
                    w->lists->trusted.Append(platform::JoinPath(w->folder, e.name));
                },
                &walk);
        }

        bool Start(const DotnetHostDesc& desc, DotnetRuntime& out, String& error)
        {
            // Never closed: the runtime cannot be unloaded, and a library closed at exit would pull it from under
            // its own threads.
            static platform::DynamicLibrary& library = *new platform::DynamicLibrary();
            String nativeFolder(desc.nativeFolder), frameworkFolder(desc.frameworkFolder), root(desc.dotnetRoot);
            if (nativeFolder.Empty() || frameworkFolder.Empty())
            {
                // The installed shared framework (the highest Microsoft.NETCore.App of the first root that has one).
                Array<String> roots;
                if (!desc.dotnetRoot.Empty())
                    roots.Push(String(desc.dotnetRoot));
                else
                    FindDotnetRoots(roots);
                String found;
                for (const String& r : roots)
                {
                    found = FindFramework(r, "Microsoft.NETCore.App", StringView());
                    if (!found.Empty())
                    {
                        root = r;
                        break;
                    }
                }
                if (found.Empty())
                    return Fail(error, "no shared framework found for the coreclr runtime");
                if (nativeFolder.Empty())
                    nativeFolder = found;
                if (frameworkFolder.Empty())
                    frameworkFolder = found;
            }
            String libraryPath = platform::JoinPath(nativeFolder, kLibrary);
            if (!library.Open(libraryPath))
                return Fail(error, Format("{} does not load", libraryPath));
            auto initialize = library.Function<InitializeFn>("coreclr_initialize");
            auto createDelegate = library.Function<CreateDelegateFn>("coreclr_create_delegate");
            if (!initialize || !createDelegate)
                return Fail(error, Format("{} lacks coreclr_initialize or coreclr_create_delegate", libraryPath));

            // The framework's assemblies and the managed side (when it is not one of them) are trusted; the managed
            // side's folder is the app's.
            Lists lists;
            AddAssemblies(frameworkFolder, lists);
            String runtimeAssembly(desc.runtimeAssembly);
            if (!lists.haveRuntime)
            {
                if (!lists.trusted.Empty())
                    lists.trusted.Append(StringView(&kPathSeparator, 1));
                lists.trusted.Append(runtimeAssembly);
            }
            String appPaths(platform::GetDirectoryName(runtimeAssembly));
            String nativePaths = nativeFolder;
            if (nativeFolder != frameworkFolder)
            {
                nativePaths.Append(StringView(&kPathSeparator, 1));
                nativePaths.Append(frameworkFolder);
            }
            const char* keys[] = {"TRUSTED_PLATFORM_ASSEMBLIES", "APP_PATHS", "NATIVE_DLL_SEARCH_DIRECTORIES", "System.Globalization.Invariant",
                                  "System.Runtime.TieredPGO"};
            lists.trusted = NativePath(Move(lists.trusted));
            appPaths = NativePath(Move(appPaths));
            nativePaths = NativePath(Move(nativePaths));
            const char* values[] = {lists.trusted.CStr(), appPaths.CStr(), nativePaths.CStr(), "true", "true"};
            void* host = nullptr;
            u32 domain = 0;
            String exe = NativePath(platform::ExecutablePath());
            i32 rc = initialize(exe.CStr(), "Helix", i32(sizeof(keys) / sizeof(keys[0])), keys, values, &host, &domain);
            if (rc < 0 || !host)
                return Fail(error, Format("coreclr_initialize failed (0x{:08x})", u32(rc)));
            String assembly(kManagedAssembly), type(kManagedType), method(kManagedMethod);
            void* function = nullptr;
            rc = createDelegate(host, domain, assembly.CStr(), type.CStr(), method.CStr(), &function);
            if (rc < 0 || !function)
                return Fail(error, Format("Helix.Scripting.Host.Initialize is not in {} (0x{:08x})", desc.runtimeAssembly, u32(rc)));
            out.initialize = std::bit_cast<ManagedInitializeFn>(function);
            out.root = root.Empty() ? nativeFolder : root;
            return true;
        }
    }

    const DotnetRuntimeStarter kCoreclrRuntime{"coreclr", &Start};
}

#endif
