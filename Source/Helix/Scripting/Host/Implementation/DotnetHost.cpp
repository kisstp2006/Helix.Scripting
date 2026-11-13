#include <Helix/Scripting/Host/DotnetHost.h>

#include <Helix/Core/Strings/Format.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/System/Process.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Host/Implementation/DotnetRuntimes.h>

namespace helix::scripting
{
    namespace detail
    {
        LogChannel gHostLog{"Dotnet"};

        String*& CallError()
        {
            thread_local String* error = nullptr;
            return error;
        }

        bool Fail(String& error, String text)
        {
            error = Move(text);
            HX_LOG_ERROR(gHostLog, "{}", error);
            return false;
        }

        i32 CompareVersions(StringView a, StringView b)
        {
            auto split = [](StringView v, StringView& numbers, StringView& pre) {
                usize dash = v.Find('-');
                numbers = dash == StringView::npos ? v : v.Substr(0, dash);
                pre = dash == StringView::npos ? StringView() : v.Substr(dash + 1);
            };
            auto compareParts = [](StringView x, StringView y, bool numeric) {
                usize i = 0, j = 0;
                while (i < x.Size() || j < y.Size())
                {
                    if (i >= x.Size())
                        return -1;
                    if (j >= y.Size())
                        return 1;
                    usize ie = i, je = j;
                    while (ie < x.Size() && x[ie] != '.')
                        ie++;
                    while (je < y.Size() && y[je] != '.')
                        je++;
                    StringView px = x.Substr(i, ie - i), py = y.Substr(j, je - j);
                    bool nx = !px.Empty() && px[0] >= '0' && px[0] <= '9', ny = !py.Empty() && py[0] >= '0' && py[0] <= '9';
                    if ((numeric || (nx && ny)) && px.Size() != py.Size())
                        return px.Size() < py.Size() ? -1 : 1;
                    i32 c = px.Compare(py);
                    if (c != 0)
                        return c < 0 ? -1 : 1;
                    i = ie + 1;
                    j = je + 1;
                }
                return 0;
            };
            StringView na, pa, nb, pb;
            split(a, na, pa);
            split(b, nb, pb);
            if (i32 c = compareParts(na, nb, true))
                return c;
            if (pa.Empty() != pb.Empty())
                return pa.Empty() ? 1 : -1;
            return compareParts(pa, pb, false);
        }

        String FindFramework(StringView root, StringView framework, StringView version)
        {
            String dir = platform::JoinPath(platform::JoinPath(root, "shared"), framework);
            if (!platform::DirectoryExists(dir))
                return String();
            struct Search
            {
                StringView version;
                StringView major;
                String best;
            } search;
            search.version = version;
            usize dot = version.Find('.');
            search.major = dot == StringView::npos ? version : version.Substr(0, dot + 1);
            platform::ListDirectory(
                dir,
                [](const platform::DirectoryEntry& e, void* user) {
                    auto* s = static_cast<Search*>(user);
                    if (!e.isDirectory)
                        return;
                    if (!s->version.Empty() && (!e.name.StartsWith(s->major) || CompareVersions(e.name, s->version) < 0))
                        return;
                    if (s->best.Empty() || CompareVersions(e.name, s->best) > 0)
                        s->best = String(e.name);
                },
                &search);
            return search.best.Empty() ? String() : platform::JoinPath(dir, search.best);
        }

        Span<const DotnetRuntimeStarter> BuiltinRuntimes()
        {
            static const DotnetRuntimeStarter starters[] = {
#if HX_PLATFORM_WINDOWS || HX_PLATFORM_LINUX
                kHostfxrRuntime,
                kCoreclrRuntime,
#elif HX_PLATFORM_ANDROID
                kCoreclrRuntime,
#elif HX_PLATFORM_WEB
                kBrowserRuntime,
#endif
            };
            return Span<const DotnetRuntimeStarter>(starters, sizeof(starters) / sizeof(starters[0]));
        }
    }

    namespace
    {
        using namespace detail;

        // The tables the two sides exchange in Host.Initialize (the same layout in Helix.Scripting.Runtime).
        struct NativeApi
        {
            u32 size = sizeof(NativeApi);
            u32 version = 1;
            void (*log)(i32 level, const char* text, i32 size);
            void (*setError)(const char* text, i32 size);
            const void* (*findExport)(const char* name, i32 size);
            const void* const* (*nativeFunctions)(const char* module, i32 size, u32* count);
        };

        struct ManagedApi
        {
            u32 size = sizeof(ManagedApi);
            u32 version = 1;
            void (*shutdown)();
            i32 (*loadAssembly)(const char* path, i32 size, u64* handle);
            i32 (*getFunction)(u64 assembly, const char* type, i32 typeSize, const char* method, i32 methodSize, void** function);
            u64 (*retainHandle)(u64 handle);
            void (*releaseHandle)(u64 handle);
            u64 (*weakHandle)(u64 handle);
            u8 (*handleAlive)(u64 handle);
            void (*collect)();
        };

        struct State
        {
            Array<DotnetRuntimeStarter> starters;
            bool registered = false;
            DotnetRuntime runtime;
            String runtimeName;
            bool started = false;
            bool running = false;
            NativeApi native{};
            ManagedApi managed{};
        };

        State& S()
        {
            static State s;
            if (!s.registered)
            {
                s.registered = true;
                for (const DotnetRuntimeStarter& starter : BuiltinRuntimes())
                    s.starters.Push(starter);
            }
            return s;
        }

        void NativeLog(i32 level, const char* text, i32 size)
        {
            LogLevel l = level <= 0 ? LogLevel::Trace : level >= i32(LogLevel::Fatal) ? LogLevel::Fatal : LogLevel(level);
            WriteLog(gHostLog, l, __FILE__, __LINE__, StringView(text, usize(size)));
        }

        void NativeSetError(const char* text, i32 size)
        {
            if (String* e = CallError())
                *e = String(StringView(text, usize(size)));
        }

        const void* NativeFindExport(const char* name, i32 size) { return FindNativeExport(StringView(name, usize(size))); }

        const void* const* NativeFunctionTable(const char* module, i32 size, u32* count)
        {
            Span<const void* const> t = NativeFunctions(StringView(module, usize(size)));
            *count = u32(t.Size());
            return t.Data();
        }
    }

    void RegisterDotnetRuntime(const DotnetRuntimeStarter& starter)
    {
        State& s = S();
        for (DotnetRuntimeStarter& existing : s.starters)
            if (existing.name == starter.name)
            {
                existing = starter;
                return;
            }
        s.starters.Push(starter);
    }

    const DotnetRuntimeStarter* FindDotnetRuntime(StringView name)
    {
        for (const DotnetRuntimeStarter& starter : S().starters)
            if (starter.name == name)
                return &starter;
        return nullptr;
    }

    StringView DefaultDotnetRuntime()
    {
        Span<const DotnetRuntimeStarter> builtin = BuiltinRuntimes();
        return builtin.Size() ? builtin[0].name : StringView();
    }

    void FindDotnetRoots(Array<String>& out)
    {
        String value;
        out.Push(platform::JoinPath(platform::GetDirectoryName(platform::ExecutablePath()), "dotnet"));
        if (platform::ReadEnvironment("DOTNET_ROOT", value) && !value.Empty())
            out.Push(value);
        if (platform::ReadEnvironment("HELIX_DOTNET_ROOT", value) && !value.Empty())
            out.Push(value);
#if HX_PLATFORM_WINDOWS
        if (platform::ReadEnvironment("ProgramFiles", value) && !value.Empty())
            out.Push(platform::JoinPath(value, "dotnet"));
        out.Push(String("C:/Program Files/dotnet"));
#elif HX_PLATFORM_LINUX
        for (const char* dir : {"/usr/share/dotnet", "/usr/lib/dotnet", "/usr/lib64/dotnet", "/usr/local/share/dotnet"})
            out.Push(String(dir));
        if (platform::ReadEnvironment("HOME", value) && !value.Empty())
            out.Push(platform::JoinPath(value, ".dotnet"));
#endif
    }

    bool DotnetHost::Start(const DotnetHostDesc& desc, String& error)
    {
        State& s = S();
        if (s.running)
            return true;
        if (!s.started)
        {
            String name(desc.runtime);
            String fromEnvironment;
            if (name.Empty() && platform::ReadEnvironment("HELIX_DOTNET_RUNTIME", fromEnvironment) && !fromEnvironment.Empty())
                name = Move(fromEnvironment);
            if (name.Empty())
                name = String(DefaultDotnetRuntime());
            const DotnetRuntimeStarter* starter = FindDotnetRuntime(name);
            if (!starter || !starter->start)
                return Fail(error, Format("there is no .NET runtime starter '{}' on this platform", name));
            DotnetRuntime runtime;
            if (!starter->start(desc, runtime, error))
                return false;
            if (!runtime.initialize)
                return Fail(error, Format("the runtime starter '{}' found no Helix.Scripting.Host.Initialize", name));
            s.runtime = Move(runtime);
            s.runtimeName = Move(name);
            s.started = true;
        }
        s.native.size = sizeof(NativeApi);
        s.native.version = 1;
        s.native.log = &NativeLog;
        s.native.setError = &NativeSetError;
        s.native.findExport = &NativeFindExport;
        s.native.nativeFunctions = &NativeFunctionTable;
        s.managed = ManagedApi{};
        String managedError;
        CallError() = &managedError;
        i32 rc = s.runtime.initialize(&s.native, &s.managed);
        CallError() = nullptr;
        if (rc != 0 || s.managed.size != sizeof(ManagedApi) || !s.managed.loadAssembly)
            return Fail(error, Format("the managed side did not start ({}){}{}", rc, managedError.Empty() ? "" : ": ", managedError));
        s.running = true;
        return true;
    }

    void DotnetHost::Stop()
    {
        State& s = S();
        if (!s.running)
            return;
        s.managed.shutdown();
        s.running = false;
    }

    bool DotnetHost::IsRunning() { return S().running; }

    StringView DotnetHost::DotnetRoot() { return S().runtime.root; }

    StringView DotnetHost::Runtime() { return S().runtimeName; }

    AssemblyHandle DotnetHost::LoadAssembly(StringView path, String& error)
    {
        State& s = S();
        if (!s.running)
        {
            error = String("the runtime is not running");
            return AssemblyHandle{};
        }
        u64 handle = 0;
        CallError() = &error;
        i32 rc = s.managed.loadAssembly(path.Data(), i32(path.Size()), &handle);
        CallError() = nullptr;
        return rc == 0 ? AssemblyHandle{handle} : AssemblyHandle{};
    }

    void* DotnetHost::GetFunction(AssemblyHandle assembly, StringView type, StringView method, String& error)
    {
        State& s = S();
        if (!s.running)
        {
            error = String("the runtime is not running");
            return nullptr;
        }
        void* function = nullptr;
        CallError() = &error;
        i32 rc = s.managed.getFunction(assembly.value, type.Data(), i32(type.Size()), method.Data(), i32(method.Size()), &function);
        CallError() = nullptr;
        return rc == 0 ? function : nullptr;
    }

    u64 DotnetHost::RetainHandle(u64 handle) { return S().running && handle ? S().managed.retainHandle(handle) : 0; }

    void DotnetHost::ReleaseHandle(u64 handle)
    {
        if (S().running && handle)
            S().managed.releaseHandle(handle);
    }

    u64 DotnetHost::WeakHandle(u64 handle) { return S().running && handle ? S().managed.weakHandle(handle) : 0; }

    bool DotnetHost::IsHandleAlive(u64 handle) { return S().running && handle && S().managed.handleAlive(handle) != 0; }

    void DotnetHost::CollectGarbage()
    {
        if (S().running)
            S().managed.collect();
    }
}
