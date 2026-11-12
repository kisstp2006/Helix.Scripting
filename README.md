# Helix.Scripting

The scripting core of the Helix engine: one type registry for the types of every binary module (the engine, C++ game
modules, C# assemblies), so C++ and C# scripts, serialization, messages and tools read one description of a type and
reach its members the same way.

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Scripting/Types/Type.h` | `TypeId`, `TypeKind`, `TypeInfo` and its members (`FieldInfo`, `PropertyInfo`, `MethodInfo`, `ParameterInfo`, `EventInfo`, `EnumValueInfo`, `AttributeInfo`), lookups, `IsAssignableFrom`, `TypeOf<T>()` |
| `Source/Helix/Scripting/Types/TypeBuilder.h` | Modules (`RegisterModule`, `UnloadModule`), `DeclareType`, `TypeBuilder`: how generated and hand written code registers types; `OffsetOf(&T::member)` (also for classes with virtual functions) |
| `Source/Helix/Scripting/Types/Variant.h` | `Variant`: any member value with its exact type; C#'s implicit conversions and explicit ones for text |
| `Source/Helix/Scripting/Types/Invoke.h` | `FindMethod` with C#'s overload rules, `Invoke`, fields, properties and events by reflection, `CreateInstance` |

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

Release, Ryzen 7 3750H:

| Benchmark | Result |
|---|---|
| A resolved method invoked through `Invoke` (an int argument converted, an int returned) | 42 ns |
| The same by name (`Invoke(type, "Hit", ...)`) | 89 ns |
| A resolved float field read | 24 ns |
| `FindType` by full name among 2000 types | 71 ns |

## Build

```
git clone https://github.com/kisstp2006/Helix.Scripting
git -C Helix.Scripting submodule update --init
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

## License

GPL-3.0, see `LICENSE`.
