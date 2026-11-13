#include <Helix/Core/Basics/Platform.h>

#if HX_PLATFORM_WINDOWS || HX_PLATFORM_LINUX

#include <Helix/Core/Strings/Format.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/System/DynamicLibrary.h>
#include <Helix/Platform/System/Process.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Host/Implementation/DotnetRuntimes.h>
#include <Helix/Scripting/Host/Implementation/Hostfxr.h>

#include <bit>

// The desktop's starter: the installed runtime through hostfxr, for the framework the managed side's
// runtimeconfig.json asks for.
namespace helix::scripting::detail
{
    namespace
    {
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
            if (String* e = CallError())
            {
                if (!e->Empty())
                    e->Append("; ");
                e->Append(text);
            }
            HX_LOG_ERROR(gHostLog, "hostfxr: {}", text);
        }

        // The hostfxr of the highest version in <root>/host/fxr, or empty.
        String FindHostfxr(StringView root)
        {
            String fxr = platform::JoinPath(platform::JoinPath(root, "host"), "fxr");
            if (!platform::DirectoryExists(fxr))
                return String();
            String best;
            platform::ListDirectory(
                fxr,
                [](const platform::DirectoryEntry& e, void* user) {
                    auto* b = static_cast<String*>(user);
                    if (e.isDirectory && (b->Empty() || CompareVersions(e.name, *b) > 0))
                        *b = String(e.name);
                },
                &best);
            if (best.Empty())
                return String();
            String path = platform::JoinPath(platform::JoinPath(fxr, best), hostfxr::kLibraryName);
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

        bool Start(const DotnetHostDesc& desc, DotnetRuntime& out, String& error)
        {
            // Never closed: the runtime cannot be unloaded, and a library closed at exit would pull it from under
            // its own threads.
            static platform::DynamicLibrary& library = *new platform::DynamicLibrary();
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
            String fxrPath, root, fallbackPath, fallbackRoot, tried;
            for (const String& r : roots)
            {
                String found = FindHostfxr(r);
                tried.Append(tried.Empty() ? "" : ", ");
                tried.Append(r);
                if (found.Empty())
                    continue;
                if (framework.Empty() || !FindFramework(r, framework, version).Empty())
                {
                    fxrPath = found;
                    root = r;
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
                root = fallbackRoot;
            }
            if (fxrPath.Empty())
                return Fail(error, Format("no .NET runtime found (tried: {})", tried.Empty() ? StringView("nothing") : StringView(tried)));
            if (!library.Open(fxrPath))
                return Fail(error, Format("{} does not load", fxrPath));
            auto init = library.Function<hostfxr::InitializeForRuntimeConfigFn>("hostfxr_initialize_for_runtime_config");
            auto getDelegate = library.Function<hostfxr::GetRuntimeDelegateFn>("hostfxr_get_runtime_delegate");
            auto close = library.Function<hostfxr::CloseFn>("hostfxr_close");
            auto setWriter = library.Function<hostfxr::SetErrorWriterFn>("hostfxr_set_error_writer");
            if (!init || !getDelegate || !close)
                return Fail(error, Format("{} lacks the hosting functions", fxrPath));
            if (setWriter)
                setWriter(&ErrorWriter);

            Array<hostfxr::Char> configHost = ToHost(config), rootHost = ToHost(root), exeHost = ToHost(platform::ExecutablePath());
            hostfxr::InitializeParameters parameters;
            parameters.hostPath = exeHost.Data();
            parameters.dotnetRoot = rootHost.Data();
            hostfxr::Handle handle = nullptr;
            String writerText;
            CallError() = &writerText;
            i32 rc = init(configHost.Data(), &parameters, &handle);
            CallError() = nullptr;
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
            auto loadAssembly = std::bit_cast<hostfxr::LoadAssemblyFn>(load);
            auto getFunctionPointer = std::bit_cast<hostfxr::GetFunctionPointerFn>(get);

            // The managed side, in the default load context.
            Array<hostfxr::Char> assembly = ToHost(desc.runtimeAssembly);
            CallError() = &writerText;
            rc = loadAssembly(assembly.Data(), nullptr, nullptr);
            CallError() = nullptr;
            if (rc < 0)
                return Fail(error, Format("{} does not load (0x{:08x}){}{}", desc.runtimeAssembly, u32(rc), writerText.Empty() ? "" : ": ", writerText));
            Array<hostfxr::Char> type = ToHost(Format("{}, {}", kManagedType, kManagedAssembly)), method = ToHost(kManagedMethod);
            void* initialize = nullptr;
            rc = getFunctionPointer(type.Data(), method.Data(), hostfxr::UnmanagedCallersOnly(), nullptr, nullptr, &initialize);
            if (rc < 0 || !initialize)
                return Fail(error, Format("Helix.Scripting.Host.Initialize is not in {} (0x{:08x})", desc.runtimeAssembly, u32(rc)));
            out.initialize = std::bit_cast<ManagedInitializeFn>(initialize);
            out.root = Move(root);
            return true;
        }
    }

    const DotnetRuntimeStarter kHostfxrRuntime{"hostfxr", &Start};
}

#endif
