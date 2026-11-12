#pragma once

#include <Helix/Scripting/Types/Variant.h>

#include <initializer_list>
#include <new>

// Registering types: modules, then the types of a module (declared first, so members and bases can name each other),
// then each type's members through a TypeBuilder. Generated code (Helix.Bindgen) and hand written code use the same
// calls. The registry is changed on one thread at a time; reading it is safe from any thread while it does not change.
namespace helix::scripting
{
    enum class ModuleKind : u8
    {
        // The primitive and array types.
        Builtin,
        Engine,
        // C++ game code (a shared library, or linked into the player on the Web).
        NativeGame,
        // A C# assembly.
        Managed,
    };

    struct ModuleInfo
    {
        StringView name;
        ModuleKind kind = ModuleKind::Engine;
    };

    ModuleId RegisterModule(StringView name, ModuleKind kind);
    ModuleId FindModule(StringView name);
    // Null for modules that were unloaded.
    const ModuleInfo* GetModule(ModuleId module);
    // Removes the module's types (their ids stop resolving, and the array types of them go too) and frees its members.
    void UnloadModule(ModuleId module);

    // A type without members yet; its base (if any) is declared already. Invalid when the module is unknown, the name
    // is taken, or the base does not resolve.
    TypeId DeclareType(ModuleId module, StringView nameSpace, StringView name, TypeKind kind, TypeId base = TypeId(), u32 flags = TypeNone);

    // The offset of a data member, also in classes with virtual functions (where offsetof is not defined): the member's
    // address in storage of the class's size and alignment, never constructed.
    template <class T, class M>
    u32 OffsetOf(M T::*member)
    {
        alignas(T) static unsigned char storage[sizeof(T)];
        const T* object = reinterpret_cast<const T*>(storage);
        return u32(reinterpret_cast<const unsigned char*>(&(object->*member)) - storage);
    }

    // Fills a declared type. Members are kept by the builder and given to the registry at Finish (or destruction), so
    // builders of one module may be open at the same time. Attribute adds to what was added last (the type itself
    // before any member).
    class TypeBuilder
    {
    public:
        explicit TypeBuilder(TypeId type) : type_(type) {}
        ~TypeBuilder();
        TypeBuilder(const TypeBuilder&) = delete;
        TypeBuilder& operator=(const TypeBuilder&) = delete;

        TypeBuilder& Size(u32 size, u32 alignment);
        TypeBuilder& Lifetime(Constructor construct, Destructor destroy);
        TypeBuilder& Flags(u32 flags);
        TypeBuilder& User(u64 user);
        TypeBuilder& Interface(TypeId type);
        // Enums: the underlying integer type.
        TypeBuilder& Underlying(TypeId type);

        // Size, alignment, plain bytes, construction and destruction from the C++ type, and TypeOf<T>().
        template <class T>
        TypeBuilder& Native()
        {
            Size(u32(sizeof(T)), u32(alignof(T)));
            if constexpr (std::is_trivially_copyable_v<T> && !std::is_polymorphic_v<T>)
                Flags(TypePlain);
            if constexpr (std::is_default_constructible_v<T> && std::is_destructible_v<T>)
                Lifetime([](void* m) { new (m) T(); }, [](void* m) { static_cast<T*>(m)->~T(); });
            BindNativeType<T>(type_);
            return *this;
        }

        TypeBuilder& Field(StringView name, TypeId type, u32 offset, u32 flags = MemberPublic);
        // A data member of the C++ type: its offset and its type (TypeOf).
        template <class T, class M>
        TypeBuilder& Field(StringView name, M T::*member, u32 flags = MemberPublic)
        {
            return Field(name, TypeOf<M>(), OffsetOf(member), flags);
        }
        TypeBuilder& Field(StringView name, TypeId type, FieldGetter get, FieldSetter set, const void* context, u32 flags = MemberPublic);
        TypeBuilder& Method(StringView name, TypeId returnType, std::initializer_list<ParameterInfo> parameters, Invoker invoke,
                            const void* context = nullptr, u32 flags = MemberPublic);
        TypeBuilder& Method(StringView name, TypeId returnType, Span<const ParameterInfo> parameters, Invoker invoke, const void* context = nullptr,
                            u32 flags = MemberPublic);
        // A property with a getter and / or a setter method (get_Name, set_Name).
        TypeBuilder& Property(StringView name, TypeId type, Invoker getter, Invoker setter, const void* context = nullptr, u32 flags = MemberPublic);
        // An event with add, remove and raise methods (add_Name, remove_Name, raise_Name), each taking a handler.
        TypeBuilder& Event(StringView name, TypeId handlerType, Invoker add, Invoker remove, Invoker raise, const void* context = nullptr,
                           u32 flags = MemberPublic);
        TypeBuilder& EnumValue(StringView name, i64 value);
        TypeBuilder& Attribute(TypeId type, std::initializer_list<Variant> arguments = {});
        TypeBuilder& Attribute(TypeId type, Span<const Variant> arguments);

        // Gives the members to the registry (once); the type id.
        TypeId Finish();

    private:
        enum class Target : u8
        {
            Type,
            Field,
            Method,
            Property,
            Event,
        };

        struct Staged
        {
            Target target;
            u32 index;
            TypeId type;
            Array<Variant> arguments;
        };

        TypeId type_;
        bool done_ = false;
        Target last_ = Target::Type;
        u32 size_ = 0, alignment_ = 0, flags_ = 0;
        bool sized_ = false, flagged_ = false;
        Constructor construct_ = nullptr;
        Destructor destroy_ = nullptr;
        u64 user_ = 0;
        bool hasUser_ = false;
        TypeId underlying_;
        Array<TypeId> interfaces_ = Array<TypeId>();
        Array<FieldInfo> fields_ = Array<FieldInfo>();
        Array<MethodInfo> methods_ = Array<MethodInfo>();
        Array<Array<ParameterInfo>> parameters_ = Array<Array<ParameterInfo>>();
        Array<PropertyInfo> properties_ = Array<PropertyInfo>();
        Array<EventInfo> events_ = Array<EventInfo>();
        Array<EnumValueInfo> enumValues_ = Array<EnumValueInfo>();
        Array<Staged> attributes_ = Array<Staged>();
        // Names, kept (each in a block of its own, so they do not move) until Finish copies them into the module.
        Array<char*> names_ = Array<char*>();

        StringView Keep(StringView name);
    };
}
