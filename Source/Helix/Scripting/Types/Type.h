#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>

// The scripting core's type registry: every type of every binary module (the engine, C++ game modules, C# assemblies)
// with its members, so both languages, serialization, SendMessage and the tools read one description. Members live in
// flat arrays of their module; a type holds ranges into them.
namespace helix::scripting
{
    class Variant;

    // A type slot and its generation: the id of a type whose module was unloaded no longer resolves.
    struct TypeId
    {
        u32 value = 0;

        bool IsValid() const { return value != 0; }
        u32 Slot() const { return value & 0xfffff; }
        bool operator==(const TypeId&) const = default;
    };

    struct ModuleId
    {
        u32 value = 0;

        bool IsValid() const { return value != 0; }
        bool operator==(const ModuleId&) const = default;
    };

    enum class TypeKind : u8
    {
        Primitive,
        Class,
        Struct,
        Enum,
        Interface,
        // An array of an element type (Array<T>, T[], List<T>): made on demand by ArrayOf.
        Array,
    };

    // The built in types, registered first in this order (their TypeIds are fixed).
    enum class Primitive : u8
    {
        Void = 1,
        Bool,
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float,
        Double,
        String,
        // A TypeId as a value.
        Type,
        Count,
    };

    enum TypeFlags : u32
    {
        TypeNone = 0,
        TypeAbstract = 1,
        TypeSealed = 2,
        // Plain bytes: a value is copied by memcpy (structs, enums, the numeric primitives).
        TypePlain = 4,
    };

    enum MemberFlags : u32
    {
        MemberNone = 0,
        MemberPublic = 1,
        MemberStatic = 2,
        MemberVirtual = 4,
        MemberAbstract = 8,
        MemberReadOnly = 16,
        MemberConstructor = 32,
        // Written by serialization (C# public or [SerializeField], C++ HX_FIELD).
        MemberSerialized = 64,
    };

    enum ParameterFlags : u32
    {
        ParameterNone = 0,
        ParameterRef = 1,
        ParameterOut = 2,
        ParameterOptional = 4,
    };

    // Reads and writes of a field by functions (when its value is not plain bytes at an offset). `context` is what the
    // field was registered with. Errors are text, never asserts.
    using FieldGetter = bool (*)(const void* context, const void* instance, Variant& out, String& error);
    using FieldSetter = bool (*)(const void* context, void* instance, const Variant& value, String& error);
    // Calls a method: `instance` is null for static methods; the arguments are already of the parameters' types.
    using Invoker = bool (*)(const void* context, void* instance, Span<const Variant> args, Variant& result, String& error);

    inline constexpr u32 kNoOffset = ~0u;
    inline constexpr u32 kNoMember = ~0u;

    // A range of a module's member array.
    struct Range
    {
        u32 first = 0;
        u32 count = 0;
    };

    struct AttributeInfo
    {
        TypeId type;
        // Its arguments, in the module's argument array.
        Range args;
    };

    struct FieldInfo
    {
        StringView name;
        TypeId owner;
        TypeId type;
        u32 flags = MemberNone;
        // Plain fields: the value's bytes at this offset of the instance; kNoOffset for fields by accessors.
        u32 offset = kNoOffset;
        FieldGetter get = nullptr;
        FieldSetter set = nullptr;
        const void* context = nullptr;
        Range attributes;
    };

    struct ParameterInfo
    {
        StringView name;
        TypeId type;
        u32 flags = ParameterNone;
    };

    struct MethodInfo
    {
        StringView name;
        TypeId owner;
        TypeId returnType;
        u32 flags = MemberNone;
        Range parameters;
        Invoker invoke = nullptr;
        const void* context = nullptr;
        Range attributes;
    };

    // A getter and a setter, as methods of the type (kNoMember when missing).
    struct PropertyInfo
    {
        StringView name;
        TypeId owner;
        TypeId type;
        u32 flags = MemberNone;
        u32 getter = kNoMember;
        u32 setter = kNoMember;
        Range attributes;
    };

    // Add, remove and raise, as methods of the type (kNoMember when missing); a handler is a Variant of `handlerType`.
    struct EventInfo
    {
        StringView name;
        TypeId owner;
        TypeId handlerType;
        u32 flags = MemberNone;
        u32 add = kNoMember;
        u32 remove = kNoMember;
        u32 raise = kNoMember;
        Range attributes;
    };

    struct EnumValueInfo
    {
        StringView name;
        i64 value = 0;
    };

    // Making and freeing an instance in memory of the type's size and alignment (null for types without them).
    using Constructor = void (*)(void* memory);
    using Destructor = void (*)(void* memory);

    struct TypeInfo
    {
        StringView name;
        StringView nameSpace;
        // Namespace and name joined by a dot.
        StringView fullName;
        ModuleId module;
        TypeKind kind = TypeKind::Class;
        u32 flags = TypeNone;
        TypeId base;
        // Arrays: the element type; enums: the underlying integer type.
        TypeId element;
        u32 size = 0;
        u32 alignment = 0;
        Constructor construct = nullptr;
        Destructor destroy = nullptr;
        // What the module keeps with the type (the engine: its class index).
        u64 user = 0;
        Range interfaces;
        Range fields;
        Range properties;
        Range methods;
        Range events;
        Range enumValues;
        Range attributes;
        // Depth first numbers over the bases: a type derives from another when its number is in the other's range.
        u32 order = 0;
        u32 orderEnd = 0;
    };

    // ------------------------------------------------------------------------------------------------- lookups
    // Null for ids that do not resolve (never registered, or of an unloaded module).
    const TypeInfo* GetType(TypeId type);
    TypeId TypeOfPrimitive(Primitive primitive);
    // By "Namespace.Name" (or "Name" without a namespace).
    TypeId FindType(StringView fullName);
    // The array type of an element type, made on first use.
    TypeId ArrayOf(TypeId element);
    // Every type of a module, in registration order.
    void TypesOfModule(ModuleId module, Array<TypeId>& out);

    // `source` is `target` or derives from it or implements it (an interface).
    bool IsAssignableFrom(TypeId target, TypeId source);

    Span<const TypeId> Interfaces(TypeId type);
    Span<const FieldInfo> Fields(TypeId type);
    Span<const PropertyInfo> Properties(TypeId type);
    Span<const MethodInfo> Methods(TypeId type);
    Span<const EventInfo> Events(TypeId type);
    Span<const EnumValueInfo> EnumValues(TypeId type);
    Span<const AttributeInfo> Attributes(TypeId type);
    Span<const ParameterInfo> Parameters(TypeId owner, const MethodInfo& method);
    Span<const AttributeInfo> Attributes(TypeId owner, Range attributes);
    Span<const Variant> AttributeArguments(TypeId owner, const AttributeInfo& attribute);

    // The members of a type and of its bases (the type's own first): null when none has the name.
    const FieldInfo* FindField(TypeId type, StringView name);
    const PropertyInfo* FindProperty(TypeId type, StringView name);
    const EventInfo* FindEvent(TypeId type, StringView name);
    // The owner's method of an index of PropertyInfo / EventInfo.
    const MethodInfo* MethodAt(TypeId owner, u32 index);
    // The first attribute of the type in the range, or null.
    const AttributeInfo* FindAttribute(TypeId owner, Range attributes, TypeId attributeType);

    // ------------------------------------------------------------------------------------------------- C++ types
    namespace detail
    {
        template <class T>
        struct TypeSlot
        {
            static inline TypeId id;
        };
    }

    // The type registered for a C++ type (TypeBuilder::Native), or invalid.
    template <class T>
    TypeId TypeOf()
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::is_same_v<U, void>)
            return TypeOfPrimitive(Primitive::Void);
        else if constexpr (std::is_same_v<U, bool>)
            return TypeOfPrimitive(Primitive::Bool);
        else if constexpr (std::is_same_v<U, i8>)
            return TypeOfPrimitive(Primitive::Int8);
        else if constexpr (std::is_same_v<U, u8>)
            return TypeOfPrimitive(Primitive::UInt8);
        else if constexpr (std::is_same_v<U, i16>)
            return TypeOfPrimitive(Primitive::Int16);
        else if constexpr (std::is_same_v<U, u16>)
            return TypeOfPrimitive(Primitive::UInt16);
        else if constexpr (std::is_same_v<U, i32>)
            return TypeOfPrimitive(Primitive::Int32);
        else if constexpr (std::is_same_v<U, u32>)
            return TypeOfPrimitive(Primitive::UInt32);
        else if constexpr (std::is_same_v<U, i64>)
            return TypeOfPrimitive(Primitive::Int64);
        else if constexpr (std::is_same_v<U, u64>)
            return TypeOfPrimitive(Primitive::UInt64);
        else if constexpr (std::is_same_v<U, f32>)
            return TypeOfPrimitive(Primitive::Float);
        else if constexpr (std::is_same_v<U, f64>)
            return TypeOfPrimitive(Primitive::Double);
        else if constexpr (std::is_same_v<U, String> || std::is_same_v<U, StringView>)
            return TypeOfPrimitive(Primitive::String);
        else if constexpr (std::is_same_v<U, TypeId>)
            return TypeOfPrimitive(Primitive::Type);
        else
            return detail::TypeSlot<U>::id;
    }

    // Makes the type of a C++ type known to TypeOf<T>.
    template <class T>
    void BindNativeType(TypeId type)
    {
        detail::TypeSlot<std::remove_cv_t<T>>::id = type;
    }
}
