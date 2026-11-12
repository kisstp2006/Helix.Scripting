#include <Helix/Scripting/Host/DotnetHost.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/System/DynamicLibrary.h>
#include <Helix/Platform/System/Process.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Host/Implementation/Hostfxr.h>

#include <bit>
#include <cstring>

namespace helix::scripting
{
#if HX_PLATFORM_WINDOWS || HX_PLATFORM_LINUX
    namespace
    {
        LogChannel gHostLog{"Dotnet"};

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

        using InitializeFn = i32 (*)(NativeApi* native, ManagedApi* managed);

        struct State
        {
            platform::DynamicLibrary hostfxr;
            hostfxr::LoadAssemblyFn loadAssembly = nullptr;
            hostfxr::GetFunctionPointerFn getFunctionPointer = nullptr;
            InitializeFn initialize = nullptr;
            String root;
            String runtimeAssembly;
            bool runtime = false;
            bool running = false;
            NativeApi native{};
            ManagedApi managed{};
        };

        State& S()
        {
            static State s;
            return s;
        }

        // What the managed side reports as the error of the call in progress, and what hostfxr writes.
        thread_local String* tError = nullptr;

        Array<hostfxr::Char> ToHost(StringView text)
        {
            Array<hostfxr::Char> out;
#if HX_PLATFORM_WINDOWS
            // UTF-8 to UTF-16.
            for (usize i = 0; i < text.Size();)
            {
                u32 c = u8(text[i]);
                usize n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
                if (n > 1)
                    c &= (0xFF >> (n + 1));
                for (usize k = 1; k < n && i + k < text.Size(); k++)
                    c = (c << 6) | (u8(text[i + k]) & 0x3F);
                i += n;
                if (c >= 0x10000)
                {
                    c -= 0x10000;
                    out.Push(hostfxr::Char(0xD800 + (c >> 10)));
                    out.Push(hostfxr::Char(0xDC00 + (c & 0x3FF)));
                }
                else
                    out.Push(hostfxr::Char(c));
            }
#else
            for (char c : text)
                out.Push(c);
#endif
            out.Push(0);
            return out;
        }

        String FromHost(const hostfxr::Char* text)
        {
            if (!text)
                return String();
#if HX_PLATFORM_WINDOWS
            i32 n = 0;
            while (text[n])
                n++;
            return StringFromUtf16(reinterpret_cast<const char16_t*>(text), n);
#else
            return String(StringView(text));
#endif
        }

        void ErrorWriter(const hostfxr::Char* message)
        {
            String text = FromHost(message);
            if (tError)
            {
                if (!tError->Empty())
                    tError->Append("; ");
                tError->Append(text);
            }
            HX_LOG_ERROR(gHostLog, "hostfxr: {}", text);
        }

        // ------------------------------------------------------------------------------------- the native table

        void NativeLog(i32 level, const char* text, i32 size)
        {
            LogLevel l = level <= 0 ? LogLevel::Trace : level >= i32(LogLevel::Fatal) ? LogLevel::Fatal : LogLevel(level);
            WriteLog(gHostLog, l, __FILE__, __LINE__, StringView(text, usize(size)));
        }

        void NativeSetError(const char* text, i32 size)
        {
            if (tError)
                *tError = String(StringView(text, usize(size)));
        }

        const void* NativeFindExport(const char* name, i32 size) { return FindNativeExport(StringView(name, usize(size))); }

        const void* const* NativeFunctionTable(const char* module, i32 size, u32* count)
        {
            Span<const void* const> t = NativeFunctions(StringView(module, usize(size)));
            *count = u32(t.Size());
            return t.Data();
        }

        // ------------------------------------------------------------------------------------- finding the runtime

        // "11.0.0-rc.3.26522.148" against another: the numbers, then a release over a prerelease, then the
        // prerelease parts (numbers by value).
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

        struct Versions
        {
            String best;
        };

        // The hostfxr of the highest version in <root>/host/fxr, or empty.
        String FindHostfxr(StringView root)
        {
            String fxr = platform::JoinPath(platform::JoinPath(root, "host"), "fxr");
            if (!platform::DirectoryExists(fxr))
                return String();
            Versions v;
            platform::ListDirectory(
                fxr,
                [](const platform::DirectoryEntry& e, void* user) {
                    auto* versions = static_cast<Versions*>(user);
                    if (e.isDirectory && (versions->best.Empty() || CompareVersions(e.name, versions->best) > 0))
                        versions->best = String(e.name);
                },
                &v);
            if (v.best.Empty())
                return String();
            String path = platform::JoinPath(platform::JoinPath(fxr, v.best), hostfxr::kLibraryName);
            return platform::FileExists(path) ? path : String();
        }

        // The framework of a runtimeconfig.json ("framework": {"name": ..., "version": ...}).
        void ReadFramework(StringView json, String& name, String& version)
        {
            auto value = [&](StringView key, usize from) {
                usize k = json.Find(key, from);
                if (k == StringView::npos)
                    return StringView();
                usize colon = json.Find(':', k + key.Size());
                usize open = colon == StringView::npos ? StringView::npos : json.Find('"', colon);
                usize close = open == StringView::npos ? StringView::npos : json.Find('"', open + 1);
                return close == StringView::npos ? StringView() : json.Substr(open + 1, close - open - 1);
            };
            usize at = json.Find("\"framework\"");
            if (at == StringView::npos)
                return;
            name = String(value("\"name\"", at));
            version = String(value("\"version\"", at));
        }

        struct FrameworkSearch
        {
            StringView version;
            StringView major;
            bool found = false;
        };

        // <root>/shared/<name> has a version of the same major version and not below it.
        bool HasFramework(StringView root, StringView name, StringView version)
        {
            if (name.Empty())
                return true;
            String dir = platform::JoinPath(platform::JoinPath(root, "shared"), name);
            if (!platform::DirectoryExists(dir))
                return false;
            FrameworkSearch search;
            search.version = version;
            usize dot = version.Find('.');
            search.major = dot == StringView::npos ? version : version.Substr(0, dot + 1);
            platform::ListDirectory(
                dir,
                [](const platform::DirectoryEntry& e, void* user) {
                    auto* f = static_cast<FrameworkSearch*>(user);
                    if (e.isDirectory && e.name.StartsWith(f->major) && CompareVersions(e.name, f->version) >= 0)
                        f->found = true;
                },
                &search);
            return search.found;
        }

        bool Fail(String& error, String text)
        {
            error = Move(text);
            HX_LOG_ERROR(gHostLog, "{}", error);
            return false;
        }

        // Loads hostfxr, initializes the runtime for the managed side's configuration and takes the two delegates.
        bool StartRuntime(State& s, const DotnetHostDesc& desc, String& error)
        {
            String config(desc.runtimeAssembly);
            if (config.View().EndsWith(".dll"))
                config = String(config.View().Substr(0, config.Size() - 4));
            config.Append(".runtimeconfig.json");
            Array<u8> configText;
            if (!platform::ReadFile(config, configText))
                return Fail(error, Format("{} is not there", config));
            String framework, version;
            ReadFramework(StringView(reinterpret_cast<const char*>(configText.Data()), configText.Size()), framework, version);

            Array<String> roots;
            if (!desc.dotnetRoot.Empty())
                roots.Push(String(desc.dotnetRoot));
            else
                FindDotnetRoots(roots);
            // The first root with hostfxr and the framework the configuration asks for (its major version, at least
            // its version); else the first with hostfxr, whose error then says what is missing.
            String fxrPath, fallbackPath, fallbackRoot;
            String tried;
            for (const String& r : roots)
            {
                String found = FindHostfxr(r);
                tried.Append(tried.Empty() ? "" : ", ");
                tried.Append(r);
                if (found.Empty())
                    continue;
                if (HasFramework(r, framework, version))
                {
                    fxrPath = found;
                    s.root = r;
                    break;
                }
                if (fallbackPath.Empty())
                {
                    fallbackPath = found;
                    fallbackRoot = r;
                }
            }
            if (fxrPath.Empty() && !fallbackPath.Empty())
            {
                fxrPath = fallbackPath;
                s.root = fallbackRoot;
            }
            if (fxrPath.Empty())
                return Fail(error, Format("no .NET runtime found (tried: {})", tried.Empty() ? StringView("nothing") : StringView(tried)));
            if (!s.hostfxr.Open(fxrPath))
                return Fail(error, Format("{} does not load", fxrPath));
            auto init = s.hostfxr.Function<hostfxr::InitializeForRuntimeConfigFn>("hostfxr_initialize_for_runtime_config");
            auto getDelegate = s.hostfxr.Function<hostfxr::GetRuntimeDelegateFn>("hostfxr_get_runtime_delegate");
            auto close = s.hostfxr.Function<hostfxr::CloseFn>("hostfxr_close");
            auto setWriter = s.hostfxr.Function<hostfxr::SetErrorWriterFn>("hostfxr_set_error_writer");
            if (!init || !getDelegate || !close)
                return Fail(error, Format("{} lacks the hosting functions", fxrPath));
            if (setWriter)
                setWriter(&ErrorWriter);

            Array<hostfxr::Char> configHost = ToHost(config), rootHost = ToHost(s.root), exeHost = ToHost(platform::ExecutablePath());
            hostfxr::InitializeParameters parameters;
            parameters.hostPath = exeHost.Data();
            parameters.dotnetRoot = rootHost.Data();
            hostfxr::Handle handle = nullptr;
            String writerText;
            tError = &writerText;
            i32 rc = init(configHost.Data(), &parameters, &handle);
            tError = nullptr;
            // 0, and 1 or 2 when the runtime was started already in the process, are successes.
            if (rc < 0 || !handle)
            {
                if (handle)
                    close(handle);
                return Fail(error, Format("the runtime does not start for {} (0x{:08x}){}{}", config, u32(rc), writerText.Empty() ? "" : ": ", writerText));
            }
            void* load = nullptr;
            void* get = nullptr;
            i32 a = getDelegate(handle, hostfxr::LoadAssembly, &load);
            i32 b = getDelegate(handle, hostfxr::GetFunctionPointer, &get);
            close(handle);
            if (a < 0 || b < 0 || !load || !get)
                return Fail(error, Format("the runtime gives no assembly loading (0x{:08x}, 0x{:08x})", u32(a), u32(b)));
            s.loadAssembly = std::bit_cast<hostfxr::LoadAssemblyFn>(load);
            s.getFunctionPointer = std::bit_cast<hostfxr::GetFunctionPointerFn>(get);

            // The managed side, in the default load context.
            Array<hostfxr::Char> assembly = ToHost(desc.runtimeAssembly);
            tError = &writerText;
            rc = s.loadAssembly(assembly.Data(), nullptr, nullptr);
            tError = nullptr;
            if (rc < 0)
                return Fail(error, Format("{} does not load (0x{:08x}){}{}", desc.runtimeAssembly, u32(rc), writerText.Empty() ? "" : ": ", writerText));
            Array<hostfxr::Char> type = ToHost("Helix.Scripting.Host, Helix.Scripting.Runtime"), method = ToHost("Initialize");
            void* initialize = nullptr;
            rc = s.getFunctionPointer(type.Data(), method.Data(), hostfxr::UnmanagedCallersOnly(), nullptr, nullptr, &initialize);
            if (rc < 0 || !initialize)
                return Fail(error, Format("Helix.Scripting.Host.Initialize is not in {} (0x{:08x})", desc.runtimeAssembly, u32(rc)));
            s.initialize = std::bit_cast<InitializeFn>(initialize);
            s.runtimeAssembly = String(desc.runtimeAssembly);
            s.runtime = true;
            return true;
        }
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
#else
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
        if (!s.runtime && !StartRuntime(s, desc, error))
            return false;
        s.native.size = sizeof(NativeApi);
        s.native.version = 1;
        s.native.log = &NativeLog;
        s.native.setError = &NativeSetError;
        s.native.findExport = &NativeFindExport;
        s.native.nativeFunctions = &NativeFunctionTable;
        s.managed = ManagedApi{};
        String managedError;
        tError = &managedError;
        i32 rc = s.initialize(&s.native, &s.managed);
        tError = nullptr;
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

    StringView DotnetHost::DotnetRoot() { return S().root; }

    AssemblyHandle DotnetHost::LoadAssembly(StringView path, String& error)
    {
        State& s = S();
        if (!s.running)
        {
            error = String("the runtime is not running");
            return AssemblyHandle{};
        }
        u64 handle = 0;
        tError = &error;
        i32 rc = s.managed.loadAssembly(path.Data(), i32(path.Size()), &handle);
        tError = nullptr;
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
        tError = &error;
        i32 rc = s.managed.getFunction(assembly.value, type.Data(), i32(type.Size()), method.Data(), i32(method.Size()), &function);
        tError = nullptr;
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
#else
    // Android and the Web have hosts of their own (their steps); this one is not there.
    void FindDotnetRoots(Array<String>&) {}

    bool DotnetHost::Start(const DotnetHostDesc&, String& error)
    {
        error = String("the .NET desktop host is not on this platform");
        return false;
    }

    void DotnetHost::Stop() {}
    bool DotnetHost::IsRunning() { return false; }
    StringView DotnetHost::DotnetRoot() { return StringView(); }

    AssemblyHandle DotnetHost::LoadAssembly(StringView, String& error)
    {
        error = String("the runtime is not running");
        return AssemblyHandle{};
    }

    void* DotnetHost::GetFunction(AssemblyHandle, StringView, StringView, String& error)
    {
        error = String("the runtime is not running");
        return nullptr;
    }

    u64 DotnetHost::RetainHandle(u64) { return 0; }
    void DotnetHost::ReleaseHandle(u64) {}
    u64 DotnetHost::WeakHandle(u64) { return 0; }
    bool DotnetHost::IsHandleAlive(u64) { return false; }
    void DotnetHost::CollectGarbage() {}
#endif
}
