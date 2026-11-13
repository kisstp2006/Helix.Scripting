#include <Support/HostRuntime.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/System/Process.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Testing/Test.h>

#include <bit>
#include <chrono>
#include <cstdio>

using namespace helix;
using namespace helix::scripting;
using namespace helix::scripting::testing;

namespace
{
    i32 AddOne(i32 v) { return v + 1; }
    i32 Twice(i32 v) { return v * 2; }

    struct Captured
    {
        Array<String> errors;
    };

    void Capture(const LogMessage& message, void* user)
    {
        if (message.level >= LogLevel::Error)
            static_cast<Captured*>(user)->errors.Push(String(message.text));
    }
}

HX_TEST(Host, StartsTheRuntimeFromItsRoots)
{
    if constexpr (!kHostAvailable)
    {
        String error;
        HX_CHECK(!StartHost(error) && !error.Empty());
        return;
    }
    Array<String> roots;
    FindDotnetRoots(roots);
    // The Web's runtime is the app's; elsewhere the folders next to the program, of the environment and the defaults.
    HX_CHECK(roots.Size() >= (HX_PLATFORM_WEB ? 1u : 3u));
    String error;
    auto start = std::chrono::steady_clock::now();
    bool started = StartHost(error);
    f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
    HX_REQUIRE(started);
    if (ms > 5)
        std::printf("runtime (%.*s) and managed side started in %.1f ms from %.*s\n", int(DotnetHost::Runtime().Size()), DotnetHost::Runtime().Data(), ms,
                    int(DotnetHost::DotnetRoot().Size()), DotnetHost::DotnetRoot().Data());
    HX_CHECK(DotnetHost::IsRunning() && !DotnetHost::DotnetRoot().Empty() && !DotnetHost::Runtime().Empty());
    String wanted;
    if (platform::ReadEnvironment("HELIX_DOTNET_RUNTIME", wanted) && !wanted.Empty())
        HX_CHECK(DotnetHost::Runtime() == wanted.View());
    HX_CHECK(error.Empty());
}

HX_TEST(Host, CallsBothWays)
{
    if constexpr (!kHostAvailable)
        return;
    String error;
    AssemblyHandle tests = HostTests(error);
    HX_REQUIRE(tests.IsValid());
    auto echo = ProbeFunction<i32 (*)(i32)>(tests, "Echo", error);
    HX_REQUIRE(echo != nullptr);
    HX_CHECK(echo(41) == 42);
    auto callNative = ProbeFunction<i64 (*)(i32 (*)(i32), i32 (*)(i32), i32)>(tests, "CallNative", error);
    HX_REQUIRE(callNative != nullptr);
    // sum over i < 10 of (i + 1) + (i + 1) = 2 * 55.
    HX_CHECK(callNative(&AddOne, &AddOne, 10) == 110);

    // Native exports by name and a module's function table, from C#.
    RegisterNativeExport("HostTests.AddOne", std::bit_cast<const void*>(&AddOne));
    static const void* const table[] = {std::bit_cast<const void*>(&Twice)};
    RegisterNativeFunctions("HostTests", table, 1);
    auto exports = ProbeFunction<i32 (*)()>(tests, "CallExports", error);
    HX_REQUIRE(exports != nullptr);
    HX_CHECK(exports() == 11 + 200);

    // The test assembly lives in the engine's load context and its reference resolves from its folder.
    auto context = ProbeFunction<i32 (*)()>(tests, "InEngineContext", error);
    HX_REQUIRE(context != nullptr);
    HX_CHECK(context() == 42);
    // The same assembly loaded again is the same handle.
    HX_CHECK(HostTests(error).value == tests.value);
}

HX_TEST(Host, ErrorsAreTextAndExceptionsStayManaged)
{
    if constexpr (!kHostAvailable)
        return;
    String error;
    AssemblyHandle tests = HostTests(error);
    HX_REQUIRE(tests.IsValid());
    Captured captured;
    AddLogSink(&Capture, &captured);

    HX_CHECK(DotnetHost::GetFunction(tests, "Helix.Scripting.HostTests.Nobody", "Echo", error) == nullptr && error.View().Contains("Nobody"));
    HX_CHECK(ProbeFunction<void*>(tests, "Nothing", error) == nullptr && error.View().Contains("Nothing"));
    HX_CHECK(DotnetHost::GetFunction(tests, "Helix.Scripting.HostTests.NotAnEntry", "Plain", error) == nullptr &&
             error.View().Contains("UnmanagedCallersOnly"));
    HX_CHECK(DotnetHost::GetFunction(AssemblyHandle{999}, "A", "B", error) == nullptr && !error.Empty());
    HX_CHECK(!DotnetHost::LoadAssembly("no/such/assembly.dll", error).IsValid() && !error.Empty());
    String junk = platform::JoinPath(platform::TempDirectory(), "helix-host-junk.dll");
    const u8 bytes[] = {'n', 'o', 't', ' ', 'a', 'n', ' ', 'a', 's', 's', 'e', 'm', 'b', 'l', 'y'};
    HX_REQUIRE(platform::WriteFile(junk, Span<const u8>(bytes)));
    HX_CHECK(!DotnetHost::LoadAssembly(junk, error).IsValid() && error.View().Contains("BadImageFormat"));
    platform::RemoveFile(junk);

    // An exception thrown in C#: caught there, logged with its stack.
    captured.errors.Clear();
    auto throws = ProbeFunction<i32 (*)()>(tests, "Throws", error);
    HX_REQUIRE(throws != nullptr);
    HX_CHECK(throws() == -1);
    HX_CHECK(captured.errors.Size() == 1 && captured.errors[0].View().Contains("thrown by Probe") &&
             captured.errors[0].View().Contains("Probe.Throws"));
    RemoveLogSink(&Capture, &captured);
}

HX_TEST(Host, HandlesKeepManagedObjects)
{
    if constexpr (!kHostAvailable)
        return;
    String error;
    AssemblyHandle tests = HostTests(error);
    HX_REQUIRE(tests.IsValid());
    auto makeBox = ProbeFunction<u64 (*)(i32)>(tests, "MakeBox", error);
    auto readBox = ProbeFunction<i32 (*)(u64)>(tests, "ReadBox", error);
    HX_REQUIRE(makeBox != nullptr && readBox != nullptr);
    u64 box = makeBox(7);
    HX_REQUIRE(box != 0);
    DotnetHost::CollectGarbage();
    HX_CHECK(readBox(box) == 7);
    u64 copy = DotnetHost::RetainHandle(box);
    u64 weak = DotnetHost::WeakHandle(box);
    HX_CHECK(copy != 0 && copy != box && readBox(copy) == 7 && weak != 0 && DotnetHost::IsHandleAlive(weak));
    DotnetHost::ReleaseHandle(box);
    DotnetHost::CollectGarbage();
    HX_CHECK(DotnetHost::IsHandleAlive(weak));
    DotnetHost::ReleaseHandle(copy);
    DotnetHost::CollectGarbage();
    HX_CHECK(!DotnetHost::IsHandleAlive(weak));
    DotnetHost::ReleaseHandle(weak);
}

HX_TEST(Host, StopsAndStartsAgain)
{
    if constexpr (!kHostAvailable)
        return;
    String error;
    HX_REQUIRE(StartHost(error));
    DotnetHost::Stop();
    HX_CHECK(!DotnetHost::IsRunning());
    HX_CHECK(!DotnetHost::LoadAssembly(kHostAssembly, error).IsValid() && !error.Empty());
    AssemblyHandle tests = HostTests(error);
    HX_REQUIRE(tests.IsValid());
    auto echo = ProbeFunction<i32 (*)(i32)>(tests, "Echo", error);
    HX_CHECK(echo != nullptr && echo(1) == 2);
}
