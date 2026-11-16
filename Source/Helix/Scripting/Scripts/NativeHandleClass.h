#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Types/Type.h>

// C++ classes of a game module deriving from a value handle of another module's API (the UI's Element, say): that
// module makes and owns their objects over its own objects (an element made from markup gets its C++ part). The game
// module's generated registration describes each class here: its tags, how it is made over an object (the handle's id
// set before its constructor runs, so the constructor uses the handle) and destroyed, and its tagged fields with
// setters by kind. The module of the base takes the classes through the handler it sets for the base's name.
namespace helix::scripting
{
    // A tag of HX_CLASS or HX_FIELD as written: its key and value (empty without one, quotes taken off).
    struct NativeTag
    {
        StringView key;
        StringView value;
    };

    // The kinds of values a field's setter takes (a pointer to one of these).
    enum class NativeValueKind : u8
    {
        Bool,
        I32,
        I64,
        F32,
        F64,
        // ::helix::String
        String,
        // ::helix::Color
        Color,
    };

    struct NativeHandleField
    {
        StringView name;
        Span<const NativeTag> tags;
        NativeValueKind kind = NativeValueKind::I32;
        void (*set)(void* object, const void* value) = nullptr;
    };

    struct NativeHandleClass
    {
        // The module (its name), namespace and name the class is known by.
        StringView module;
        StringView nameSpace;
        StringView name;
        // The API handle it derives from, by its C# full name ("HelixEngine.UI.Element").
        StringView base;
        Span<const NativeTag> tags;
        u32 size = 0;
        u32 alignment = 0;
        // Makes an object in memory of the size and alignment over the base's object of the id.
        void* (*construct)(void* memory, u64 id) = nullptr;
        void (*destroy)(void* object) = nullptr;
        Span<const NativeHandleField> fields;
        // The class's own address in its module (what HandleObject<T> asks for).
        const void* key = nullptr;

        const NativeTag* Tag(StringView key) const;
    };

    // The class and its tags and fields are copied; names stay the module's (it lives as long as the class is
    // registered).
    void RegisterNativeHandleClass(const NativeHandleClass& c);
    // A module is unloaded: its classes go (the handlers hear of each).
    void UnregisterNativeHandleClasses(StringView module);
    // Every class of the base, registered and later, reaches the handler (added true, removed false).
    void SetNativeHandleHandler(StringView base, void (*handler)(const NativeHandleClass& c, bool added, void* user), void* user);
    // The object of a class (by its key) that the maker of the base made over the object of an id, null when none.
    void SetNativeHandleLookup(StringView base, void* (*lookup)(u64 id, const void* key, void* user), void* user);
    void* FindNativeHandleObject(StringView base, u64 id, const void* key);
}
