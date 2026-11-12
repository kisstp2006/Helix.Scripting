#pragma once

#include <Helix/Scripting/Types/Type.h>

#include <cstring>
#include <type_traits>

// Variant: any value a member of a registered type takes, with its exact type: void, bool, the integers, float, double,
// string, a type id, an object (a pointer and its type), a struct or enum value, an array. Scalars and structs up to 32
// bytes need no allocation.
namespace helix::scripting
{
    class Variant
    {
    public:
        static constexpr u32 kInlineBytes = 32;

        Variant() = default;
        ~Variant() { Release(); }
        Variant(const Variant& other) { CopyFrom(other); }
        Variant(Variant&& other) noexcept { MoveFrom(other); }
        Variant& operator=(const Variant& other);
        Variant& operator=(Variant&& other) noexcept;

        // ----------------------------------------------------------------------------------------- making
        static Variant Bool(bool value);
        static Variant Int(TypeId type, i64 value);
        static Variant UInt(TypeId type, u64 value);
        static Variant Real(TypeId type, f64 value);
        static Variant Text(StringView value);
        static Variant OfType(TypeId value);
        // A reference to an object of a class or interface type (null is a valid object value).
        static Variant Object(TypeId type, void* object);
        // A value of a plain type (struct, enum, numeric) from its bytes; the bytes are copied.
        static Variant Bytes(TypeId type, const void* bytes, u32 size);
        static Variant Enum(TypeId type, i64 value);
        static Variant Items(TypeId arrayType, Array<Variant> items);
        // The value of a registered type read from memory (plain bytes, or a String for the string type).
        static Variant FromMemory(TypeId type, const void* memory);

        template <class T>
        static Variant From(const T& value);

        // ----------------------------------------------------------------------------------------- reading
        TypeId Type() const { return type_; }
        bool IsVoid() const { return storage_ == Storage::None; }
        bool AsBool() const { return storage_ == Storage::Bool ? b_ : AsInt() != 0; }
        i64 AsInt() const;
        u64 AsUInt() const;
        f64 AsReal() const;
        StringView AsText() const { return storage_ == Storage::Text ? StringView(reinterpret_cast<const char*>(Payload()), size_) : StringView(); }
        TypeId AsType() const { return storage_ == Storage::TypeValue ? TypeId{u32(u_)} : TypeId(); }
        void* AsObject() const { return storage_ == Storage::Object ? object_ : nullptr; }
        // The bytes of a plain value (numbers too); size 0 otherwise.
        const void* Data() const;
        u32 DataSize() const;
        const Array<Variant>& AsItems() const;

        // Writes the value into memory holding the type (plain bytes, or a String); false when the type differs.
        bool ToMemory(TypeId type, void* memory) const;

        template <class T>
        bool Get(T& out) const;

        // An equal value of another type: the type itself, C#'s implicit numeric conversions, a null or derived
        // object to a base or interface; with `explicitConversions` also narrowing numbers, enums to and from integers
        // and numbers to and from strings (reading text values).
        bool ConvertTo(TypeId target, Variant& out, bool explicitConversions = false) const;
        // The cost of an implicit conversion: 0 the same type, 1 a conversion, -1 none (overload resolution).
        i32 ConversionCost(TypeId target) const;

        bool operator==(const Variant& other) const;

    private:
        // Kinds that own memory come last (Release checks one compare).
        enum class Storage : u8
        {
            None,
            Bool,
            Int,
            UInt,
            Real,
            TypeValue,
            Object,
            Text,
            Bytes,
            Items,
        };

        void Release()
        {
            // Scalars own nothing.
            if (storage_ >= Storage::Text)
                ReleaseSlow();
            storage_ = Storage::None;
            size_ = 0;
            type_ = TypeId();
        }
        void ReleaseSlow();
        void CopyFrom(const Variant& other);
        void MoveFrom(Variant& other);
        u8* BytesFor(u32 size);
        // Text and bytes: inline up to kInlineBytes, else on the heap.
        const u8* Payload() const { return size_ > kInlineBytes ? heap_ : inline_; }

        TypeId type_;
        Storage storage_ = Storage::None;
        u32 size_ = 0;
        union
        {
            bool b_;
            i64 i_;
            u64 u_;
            f64 d_;
            void* object_;
            u8 inline_[kInlineBytes];
            u8* heap_;
            Array<Variant>* items_;
        };
    };

    template <class T>
    Variant Variant::From(const T& value)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::is_same_v<U, bool>)
            return Bool(value);
        else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
            return Int(TypeOf<U>(), i64(value));
        else if constexpr (std::is_integral_v<U>)
            return UInt(TypeOf<U>(), u64(value));
        else if constexpr (std::is_floating_point_v<U>)
            return Real(TypeOf<U>(), f64(value));
        else if constexpr (std::is_same_v<U, String> || std::is_same_v<U, StringView>)
            return Text(value);
        else if constexpr (std::is_same_v<U, TypeId>)
            return OfType(value);
        else if constexpr (std::is_enum_v<U>)
            return Enum(TypeOf<U>(), i64(value));
        else if constexpr (std::is_pointer_v<U>)
            return Object(TypeOf<std::remove_pointer_t<U>>(), const_cast<void*>(static_cast<const void*>(value)));
        else
        {
            static_assert(std::is_trivially_copyable_v<U>, "a struct Variant holds plain bytes");
            return Bytes(TypeOf<U>(), &value, u32(sizeof(U)));
        }
    }

    template <class T>
    bool Variant::Get(T& out) const
    {
        using U = std::remove_cv_t<T>;
        Variant v;
        // An object pointer converts to the pointee's type (a base, an interface, or the type itself).
        TypeId target;
        if constexpr (std::is_pointer_v<U>)
            target = TypeOf<std::remove_cv_t<std::remove_pointer_t<U>>>();
        else
            target = TypeOf<U>();
        if (!ConvertTo(target, v))
            return false;
        if constexpr (std::is_same_v<U, bool>)
            out = v.AsBool();
        else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
            out = U(v.AsInt());
        else if constexpr (std::is_integral_v<U>)
            out = U(v.AsUInt());
        else if constexpr (std::is_floating_point_v<U>)
            out = U(v.AsReal());
        else if constexpr (std::is_same_v<U, String>)
            out = String(v.AsText());
        else if constexpr (std::is_same_v<U, TypeId>)
            out = v.AsType();
        else if constexpr (std::is_enum_v<U>)
            out = U(v.AsInt());
        else if constexpr (std::is_pointer_v<U>)
            out = static_cast<U>(v.AsObject());
        else
        {
            if (v.DataSize() != sizeof(U))
                return false;
            std::memcpy(&out, v.Data(), sizeof(U));
        }
        return true;
    }
}
