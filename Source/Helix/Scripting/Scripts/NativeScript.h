#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Types/Type.h>

// C++ script classes: C++ classes deriving from a script base class (HX_CLASS(ScriptBase)) in a binary module. The
// module's generated registration describes each one here: how its objects are made and copied, and its methods
// without arguments or with one bool, by name and as direct calls (an engine finds its messages among them: Update,
// OnApplicationFocus, ...). The engine takes the classes through the handler it sets; classes registered before the
// handler reach it when it is set.
namespace helix::scripting
{
    struct NativeScriptMethod
    {
        StringView name;
        // One of the two: no arguments, or one bool.
        void (*call)(void* instance) = nullptr;
        void (*callBool)(void* instance, bool value) = nullptr;
    };

    struct NativeScriptClass
    {
        TypeId type;
        // The module (its name), namespace and name the script is known by.
        StringView module;
        StringView nameSpace;
        StringView name;
        // The script base (HX_CLASS(ScriptBase)) the class derives from, by its registry name; the engine makes the
        // class's objects as a class below that base's.
        StringView scriptBase;
        u32 size = 0;
        u32 alignment = 0;
        // Makes an object in memory of the class's size and alignment; answers it as its root class's address.
        void* (*construct)(void* memory) = nullptr;
        // A copy of an object (given as its root class's address) in memory; answers the copy the same way.
        void* (*clone)(void* memory, const void* original) = nullptr;
        Span<const NativeScriptMethod> methods;
        // DefaultExecutionOrder (the tag argument Order).
        i32 executionOrder = 0;
        // The class's serialized fields (its base script classes' first) through a visitor of fields known at run time
        // (Helix.Serialization's DynamicVisitor); the object as its root class's address. Null without fields.
        void (*transfer)(void* instance, void* visitor) = nullptr;
    };

    // The class and its methods are copied; names stay the module's (it lives as long as the class is registered).
    void RegisterNativeScriptClass(const NativeScriptClass& script);
    // A module is unloaded: its classes go (the handler hears of each).
    void UnregisterNativeScriptClasses(StringView module);
    // Every registered class, and later ones, reach the handler (added true, removed false).
    void SetNativeScriptHandler(void (*handler)(const NativeScriptClass& script, bool added, void* user), void* user);
    void NativeScriptClasses(Array<const NativeScriptClass*>& out);
}
