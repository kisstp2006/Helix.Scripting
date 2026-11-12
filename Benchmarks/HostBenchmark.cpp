#include <Support/HostRuntime.h>

#include <Helix/Testing/Bench.h>

#include <chrono>
#include <cstdio>

// Calls across the .NET host (desktop with the SDK): native to managed, managed to native with and without the GC
// transition. The first prints how long the runtime took to start.
using namespace helix;
using namespace helix::scripting;
using namespace helix::scripting::testing;

namespace
{
    i32 AddOne(i32 v) { return v + 1; }

    AssemblyHandle Tests()
    {
        static bool printed = false;
        String error;
        auto start = std::chrono::steady_clock::now();
        AssemblyHandle tests = HostTests(error);
        if (!printed)
        {
            printed = true;
            f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (tests.IsValid())
                std::printf("runtime start, managed side and test assembly: %.1f ms\n", ms);
            else
                std::printf("no .NET host: %.*s\n", int(error.Size()), error.Data());
        }
        return tests;
    }
}

HX_BENCH(DotnetNativeToManaged)
{
    String error;
    AssemblyHandle tests = Tests();
    auto echo = tests.IsValid() ? ProbeFunction<i32 (*)(i32)>(tests, "Echo", error) : nullptr;
    i64 sum = 0;
    i32 i = 0;
    while (state.KeepRunning())
        sum += echo ? echo(i++) : i++;
    helix::testing::DoNotOptimize(sum);
}

HX_BENCH(DotnetManagedToNative)
{
    String error;
    AssemblyHandle tests = Tests();
    auto loop = tests.IsValid() ? ProbeFunction<i64 (*)(i32 (*)(i32), i32)>(tests, "LoopNative", error) : nullptr;
    state.SetItemsPerIteration(1000);
    i64 sum = 0;
    while (state.KeepRunning())
        sum += loop ? loop(&AddOne, 1000) : 0;
    helix::testing::DoNotOptimize(sum);
}

HX_BENCH(DotnetManagedToNativeLeaf)
{
    String error;
    AssemblyHandle tests = Tests();
    auto loop = tests.IsValid() ? ProbeFunction<i64 (*)(i32 (*)(i32), i32)>(tests, "LoopLeaf", error) : nullptr;
    state.SetItemsPerIteration(1000);
    i64 sum = 0;
    while (state.KeepRunning())
        sum += loop ? loop(&AddOne, 1000) : 0;
    helix::testing::DoNotOptimize(sum);
}
