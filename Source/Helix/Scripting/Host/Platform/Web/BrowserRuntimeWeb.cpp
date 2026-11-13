#include <Helix/Scripting/Host/Implementation/DotnetRuntimes.h>
#include <Helix/Scripting/Host/Platform/Web/BrowserRuntimeWeb.h>

// The Web's starter: the runtime is the app's (dotnet.js started it before native code ran); the app's C# entry
// handed over the managed side's Initialize with the program's arguments (BrowserMainWeb.cpp).
namespace helix::scripting::detail
{
    namespace
    {
        ManagedInitializeFn gInitialize = nullptr;

        bool Start(const DotnetHostDesc&, DotnetRuntime& out, String& error)
        {
            if (!gInitialize)
                return Fail(error, "the browser runtime runs only in a web app of Helix.Scripting.Browser (helix_add_executable ... DOTNET)");
            out.initialize = gInitialize;
            out.root = String("browser-wasm");
            return true;
        }
    }

    void SetBrowserInitialize(ManagedInitializeFn initialize) { gInitialize = initialize; }

    const DotnetRuntimeStarter kBrowserRuntime{"browser", &Start};
}
