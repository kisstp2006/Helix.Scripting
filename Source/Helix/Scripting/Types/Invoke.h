#pragma once

#include <Helix/Scripting/Types/Variant.h>

// Using members by reflection: finding a method by name and argument types (overloads resolved as C# does for exact
// and implicit conversions), invoking it, reading and writing fields and properties. Errors are text; nothing asserts.
namespace helix::scripting
{
    // The best method of the type or its bases for the argument types: every argument converts implicitly to its
    // parameter, the fewest conversions win, a tie is an error (null with `error` set).
    const MethodInfo* FindMethod(TypeId type, StringView name, Span<const TypeId> argumentTypes, String* error = nullptr);
    // As above for the types of the arguments themselves.
    const MethodInfo* FindMethod(TypeId type, StringView name, Span<const Variant> arguments, String* error = nullptr);

    // Converts the arguments to the parameters' types and calls the method.
    bool Invoke(const MethodInfo& method, void* instance, Span<const Variant> arguments, Variant& result, String& error);
    // FindMethod, then Invoke.
    bool Invoke(TypeId type, StringView name, void* instance, Span<const Variant> arguments, Variant& result, String& error);

    bool GetField(const FieldInfo& field, const void* instance, Variant& out, String& error);
    // The value converts implicitly (or, for fields read from text, explicitly) to the field's type.
    bool SetField(const FieldInfo& field, void* instance, const Variant& value, String& error, bool explicitConversions = false);
    bool GetProperty(const PropertyInfo& property, const void* instance, Variant& out, String& error);
    bool SetProperty(const PropertyInfo& property, void* instance, const Variant& value, String& error);

    // A field or a property by name.
    bool GetMember(TypeId type, StringView name, const void* instance, Variant& out, String& error);
    bool SetMember(TypeId type, StringView name, void* instance, const Variant& value, String& error);

    // Event handlers by name: add, remove and raise with the arguments of the event.
    bool AddHandler(TypeId type, StringView event, void* instance, const Variant& handler, String& error);
    bool RemoveHandler(TypeId type, StringView event, void* instance, const Variant& handler, String& error);
    bool RaiseEvent(TypeId type, StringView event, void* instance, Span<const Variant> arguments, String& error);

    // Makes an instance of the type in its own memory (types with a constructor); null when it has none.
    void* CreateInstance(TypeId type);
    void DestroyInstance(TypeId type, void* instance);
}
