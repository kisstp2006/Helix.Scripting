#include <Helix/Scripting/Host/Platform/Web/BrowserRuntimeWeb.h>

// The native entry of a web app of Helix.Scripting.Browser: its C# Main calls it with the arguments and the managed
// side's Initialize, and it runs the program's main (in WebAssembly main(int, char**) is __main_argc_argv). Its own
// file: only web apps reference it, so other programs do not need it.
extern "C" int __main_argc_argv(int argc, char** argv);

extern "C" int HxBrowser_Main(int argc, char** argv, void* initialize)
{
    helix::scripting::detail::SetBrowserInitialize(reinterpret_cast<helix::scripting::ManagedInitializeFn>(initialize));
    return __main_argc_argv(argc, argv);
}
