#pragma once

#include <Helix/Core/Basics/Platform.h>
#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Types/Type.h>

// What generated bindings use at the boundary between C++ and C#: object ids, strings and arrays handed out, event
// handlers, and the function tables of binary modules.
#if HX_PLATFORM_WINDOWS
#define HX_BINDINGS_EXPORT extern "C" __declspec(dllexport)
#else
#define HX_BINDINGS_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Members that take no room (the property members of value handles).
#if defined(_MSC_VER) && !defined(__clang__)
#define HX_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define HX_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace helix::scripting
{
    // Objects cross the boundary as ids (0 is null). A module's root class (HX_CLASS(Root)) specializes this:
    //   static u64 ToId(const T* object);   static T* FromId(u64 id);   static TypeId TypeOf(const T* object);
    // FromId is null for ids of destroyed objects; TypeOf is the object's dynamic type.
    template <class T>
    struct ObjectHandles;

    // What a generated thunk answers: the call ran, the object it was called on is gone (or null), or an argument
    // that must not be null was.
    enum InteropStatus : i32
    {
        InteropOk = 0,
        InteropMissingObject = 1,
        InteropNullArgument = 2,
        // A C# member threw (the exception is reported on the C# side).
        InteropManagedException = 3,
        // The function table of the other side was not handed over yet.
        InteropNotLoaded = 4,
    };

    // A failed call of generated bindings: logged as an error with the member's name.
    void ReportBindingFailure(StringView what, i32 status);

    // An object handed to managed code: its id and its dynamic type (for the wrapper of the right class).
    struct InteropObject
    {
        u64 id = 0;
        u32 type = 0;
        u32 reserved = 0;
    };

    // A string handed to managed code: UTF-8 bytes that stay valid until the thread's next call that hands one out.
    struct InteropString
    {
        const char* data = nullptr;
        i32 size = 0;
    };

    // A string handed out by managed code: UTF-16 code units valid until the thread's next call that hands one out.
    struct InteropString16
    {
        const char16_t* data = nullptr;
        i32 size = 0;
    };

    // An array handed to managed code: the items' bytes, valid until the thread's next call that hands one out.
    struct InteropArray
    {
        const void* data = nullptr;
        i32 count = 0;
    };

    // A handler of an event by reflection: the event's callback type and its user pointer (the registered struct
    // Helix.Scripting.EventHandler).
    struct EventHandler
    {
        void* function = nullptr;
        void* user = nullptr;
    };
    TypeId EventHandlerType();

    InteropString PassString(StringView text);
    InteropArray PassArray(const void* items, usize itemSize, usize count);
    // A string from managed code (UTF-16).
    String StringFromUtf16(const char16_t* data, i32 size);

    template <class Root, class T>
    T* ObjectFromId(u64 id)
    {
        return static_cast<T*>(ObjectHandles<Root>::FromId(id));
    }

    template <class Root, class T>
    InteropObject PassObject(const T* object)
    {
        if (!object)
            return InteropObject{};
        return InteropObject{ObjectHandles<Root>::ToId(object), ObjectHandles<Root>::TypeOf(object).value, 0};
    }

    // The function table a binary module's generated code exports for managed code, and the one managed code gives
    // back (the exports of its C# types for the C++ proxies), by module name.
    void RegisterNativeFunctions(StringView module, const void* const* table, u32 count);
    Span<const void* const> NativeFunctions(StringView module);
    void RegisterManagedFunctions(StringView module, void* const* table, u32 count);
    Span<void* const> ManagedFunctions(StringView module);
}
