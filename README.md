# Helix.Scripting

The scripting core of the Helix engine: one type registry for the types of every binary module (the engine, C++ game
modules, C# assemblies), so C++ and C# scripts, serialization, messages and tools read one description of a type and
reach its members the same way.

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Scripting/Types/Type.h` | `TypeId`, `TypeKind`, `TypeInfo` and its members (`FieldInfo`, `PropertyInfo`, `MethodInfo`, `ParameterInfo`, `EventInfo`, `EnumValueInfo`, `AttributeInfo`), lookups, `IsAssignableFrom`, `TypeOf<T>()` |
| `Source/Helix/Scripting/Types/TypeBuilder.h` | Modules (`RegisterModule`, `UnloadModule`), type providers (`AddTypeProvider`: types registered on first lookup), `DeclareType`, `TypeBuilder`: how generated and hand written code registers types; `OffsetOf(&T::member)` (also for classes with virtual functions) |
| `Source/Helix/Scripting/Types/Variant.h` | `Variant`: any member value with its exact type; C#'s implicit conversions and explicit ones for text |
| `Source/Helix/Scripting/Types/Invoke.h` | `FindMethod` with C#'s overload rules, `Invoke`, fields, properties and events by reflection, `CreateInstance` |
| `Source/Helix/Scripting/Bindings/Tags.h` | The tags Helix.Bindgen reads in C++ headers (`HX_CLASS`, `HX_STRUCT`, `HX_ENUM`, `HX_INTERFACE`, `HX_FIELD`, `HX_PROPERTY`, `HX_FUNCTION`, `HX_EVENT`) |
| `Source/Helix/Scripting/Bindings/Interop.h` | What generated bindings use: `ObjectHandles<Root>` (objects as ids, the wrapper slot and `ReleaseWrapper`), `Callback<R(Args...)>`, statuses, strings and arrays handed across, `EventHandler`, the function tables of modules, `ReportBindingFailure` |
| `Source/Helix/Scripting/Host/DotnetHost.h` | The .NET host: `Start` / `Stop` through a registry of runtime starters (`RegisterDotnetRuntime`: `hostfxr`, `coreclr`, `browser`), `LoadAssembly` into the engine's load context, `GetFunction` of UnmanagedCallersOnly methods, GC handles native code keeps; `FindDotnetRoots` |
| `Source/Helix/Scripting/Host/Implementation/` | The starters: `HostfxrRuntime.cpp` (Windows, Linux), `CoreclrRuntime.cpp` (`coreclr_initialize` with a framework folder: Android, and the desktop's shared framework in tests); `Host/Platform/Web/` the browser starter and `HxBrowser_Main`, the native entry of web apps |
| `Source/Managed/Helix.Scripting.Browser/` | The C# entry of a web app: hands the arguments and `Host.Initialize` to the native main |
| `CMake/HelixDotnetWeb.cmake`, `CMake/CaptureWebLink.cmake` | Web apps of .NET: an executable tagged `DOTNET` (Helix.CMake) has its link captured and is linked by a generated browser-wasm project into the runtime's module (`NativeFileReference`), with the assemblies of its `HELIX_DOTNET_ASSEMBLIES` property; `HELIX_RUN_COMMAND` runs it in node |
| `Tools/CallSignatures/` | `HelixCallSignatures`: the native function pointer calls of a web app's assemblies, declared for the build's interpreter wrappers, and the Web's limits checked |
| `Source/Managed/Helix.Scripting.Runtime/` | Its managed side (C#): `Host.Initialize` and the entry points native code calls, the engine's `AssemblyLoadContext`, `Host.Log` / `Report` / `ToNative` / `FindExport` / `NativeFunctions` for managed code |
| `Source/Helix/Scripting/Bindings/ObjectRef.h` | `ObjectRef<Root, T>`: a native object by its id with the reference's null semantics in C++ (`if (ref)`, `ref == nullptr` once destroyed) |
| `Source/Helix/Scripting/Bindings/ScriptApi.h` | What the generated C++ script API stands on: `ScriptHandle` (an object's id), `ApiRef` / `ApiAs` / `ApiTypeOf` for generic members, `ApiString16`, the module's function table by index |
| `Source/Helix/Scripting/Bindings/Marshal.h` | Values between C++ and Variants for generated invokers (objects with their dynamic type, arrays) |
| `Source/Helix/Scripting/Scripts/NativeScript.h` | `NativeScriptClass`, `NativeScriptMethod`, `RegisterNativeScriptClass`: the C++ script classes generated registration hands to the engine (`SetNativeScriptHandler`) |

## Design

- A type is declared first (its module, namespace, name, kind and base), then filled by a `TypeBuilder`, so types can
  name each other in any order. Members live in flat arrays of their module; a type holds ranges into them, and a
  type id holds a slot and a generation, so the ids of an unloaded module stop resolving.
- The primitive types (`System.Boolean` to `System.Double`, `System.String`, `System.Type`) are built in; array types
  are made on demand per element type.
- Fields are plain bytes at an offset (read and written in place) or reached through accessor functions; properties
  and events are methods of the type (`get_`, `set_`, `add_`, `remove_`, `raise_`).
- Calls by reflection pick the method as C# does: every argument converts implicitly, exact matches win over
  conversions, a better conversion target wins (long over double for an int), what remains equal is ambiguous; a
  method of a derived type hides a base method with the same parameters. Errors are text, never asserts.
- Derives-from is a range test on depth first numbers over the bases; interfaces are searched along the bases.
- The registry is changed on one thread at a time and can be read from any thread while it does not change.
- Native script classes (`NativeScriptClass`): how an engine makes and copies objects of a C++ script class, its
  methods by name, and its serialized fields through a transfer function over an opaque visitor (the engine gives it
  Helix.Serialization's `DynamicVisitor`), so this library does not depend on a serialization library.

- The .NET host finds the runtime itself (no nethost): the root it is given, `dotnet/` next to the executable,
  DOTNET_ROOT, HELIX_DOTNET_ROOT, the default install folders; the first with hostfxr and the framework that
  `Helix.Scripting.Runtime.runtimeconfig.json` asks for. The hosting interface of hostfxr is declared in
  `Host/Implementation/Hostfxr.h` from its documentation. The runtime starts once per process; `Stop` lets the
  managed side go, `Start` brings it back. Exceptions never cross the boundary: the managed entries catch them and
  log them with their stack through the native log.
- Runtime starters by name: `hostfxr` (the desktop's default), `coreclr` (the runtime library itself with the trusted
  assemblies of a framework folder: Android's default with the runtime pack's files from the APK; on the desktop the
  installed shared framework serves, and the tests run once more on it with `HELIX_DOTNET_RUNTIME=coreclr`) and
  `browser` (the Web: the runtime of the app's module, started by dotnet.js before native code runs).
- The Web: a program tagged `DOTNET` is a .NET browser-wasm app (Mono) with the program linked into
  `dotnet.native.wasm` and the engine's assemblies built in; its tests run in node. The build makes native to managed
  wrappers for the UnmanagedCallersOnly methods of the app's assemblies and managed to native ones for the signatures
  it sees, so `Tools/CallSignatures` declares every native function pointer call of them. The interpreter's limits:
  at most 12 integer slots per native call (64 bit values take two; Helix.Bindgen packs larger calls), 8 arguments to
  an UnmanagedCallersOnly method, structs by value only of one scalar field. Assemblies of the app load by name
  (`Host.LoadEngineAssembly`); script assemblies of the data folder load from the file system at run time.

Release, Ryzen 7 3750H:

| Benchmark | Result |
|---|---|
| A resolved method invoked through `Invoke` (an int argument converted, an int returned) | 42 ns |
| The same by name (`Invoke(type, "Hit", ...)`) | 89 ns |
| A resolved float field read | 24 ns |
| `FindType` by full name among 2000 types | 71 ns |
| Runtime start, the managed side and a test assembly loaded (.NET 11) | 95 ms |
| A call native to managed (UnmanagedCallersOnly) | 5.6-6.4 ns |
| A call managed to native, with the GC transition | 4.7-6.6 ns |
| The same with SuppressGCTransition (leaf calls) | 1.4-2.2 ns |

## Build

```
git clone https://github.com/kisstp2006/Helix.Scripting
git -C Helix.Scripting submodule update --init
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The host tests need the .NET 11 SDK (found through HELIX_DOTNET_ROOT, DOTNET_ROOT or the path).

## License

GPL-3.0, see `LICENSE`.
