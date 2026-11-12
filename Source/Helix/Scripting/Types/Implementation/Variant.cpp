#include <Helix/Scripting/Types/Implementation/Registry.h>

#include <Helix/Core/Memory/Allocator.h>
#include <Helix/Core/Strings/Format.h>

#include <charconv>
#include <cstdlib>
#include <cstring>

namespace helix::scripting
{
    namespace
    {
        // The primitive behind a type id, or Count for other types.
        Primitive PrimitiveOf(TypeId type)
        {
            if (!type.IsValid() || (type.value >> 20) != 1)
                return Primitive::Count;
            u32 slot = type.Slot();
            return slot >= u32(Primitive::Void) && slot < u32(Primitive::Count) ? Primitive(slot) : Primitive::Count;
        }

        bool IsSigned(Primitive p) { return p == Primitive::Int8 || p == Primitive::Int16 || p == Primitive::Int32 || p == Primitive::Int64; }
        bool IsUnsigned(Primitive p) { return p == Primitive::UInt8 || p == Primitive::UInt16 || p == Primitive::UInt32 || p == Primitive::UInt64; }
        bool IsReal(Primitive p) { return p == Primitive::Float || p == Primitive::Double; }
        bool IsNumber(Primitive p) { return IsSigned(p) || IsUnsigned(p) || IsReal(p); }

        // C#'s implicit numeric conversions (from, to).
        bool Implicit(Primitive from, Primitive to)
        {
            using P = Primitive;
            switch (from)
            {
            case P::Int8: return to == P::Int16 || to == P::Int32 || to == P::Int64 || IsReal(to);
            case P::UInt8: return to == P::Int16 || to == P::UInt16 || to == P::Int32 || to == P::UInt32 || to == P::Int64 || to == P::UInt64 || IsReal(to);
            case P::Int16: return to == P::Int32 || to == P::Int64 || IsReal(to);
            case P::UInt16: return to == P::Int32 || to == P::UInt32 || to == P::Int64 || to == P::UInt64 || IsReal(to);
            case P::Int32: return to == P::Int64 || IsReal(to);
            case P::UInt32: return to == P::Int64 || to == P::UInt64 || IsReal(to);
            case P::Int64:
            case P::UInt64: return IsReal(to);
            case P::Float: return to == P::Double;
            default: return false;
            }
        }

        // Whether an integer fits the primitive (explicit conversions refuse what does not).
        bool FitsSigned(i64 v, Primitive to)
        {
            switch (to)
            {
            case Primitive::Int8: return v >= -128 && v <= 127;
            case Primitive::UInt8: return v >= 0 && v <= 255;
            case Primitive::Int16: return v >= -32768 && v <= 32767;
            case Primitive::UInt16: return v >= 0 && v <= 65535;
            case Primitive::Int32: return v >= -2147483647 - 1 && v <= 2147483647;
            case Primitive::UInt32: return v >= 0 && v <= 4294967295ll;
            case Primitive::UInt64: return v >= 0;
            default: return true;
            }
        }

        Variant Number(Primitive to, bool isReal, bool isUnsigned, i64 i, u64 u, f64 d)
        {
            TypeId t = TypeOfPrimitive(to);
            if (IsReal(to))
                return Variant::Real(t, isReal ? d : isUnsigned ? f64(u) : f64(i));
            if (IsUnsigned(to))
                return Variant::UInt(t, isReal ? u64(d) : isUnsigned ? u : u64(i));
            return Variant::Int(t, isReal ? i64(d) : isUnsigned ? i64(u) : i);
        }
    }

    Variant& Variant::operator=(const Variant& other)
    {
        if (this != &other)
        {
            Release();
            CopyFrom(other);
        }
        return *this;
    }

    Variant& Variant::operator=(Variant&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            MoveFrom(other);
        }
        return *this;
    }

    void Variant::ReleaseSlow()
    {
        if ((storage_ == Storage::Bytes || storage_ == Storage::Text) && size_ > kInlineBytes)
            HeapAllocator().Free(heap_, size_, 16);
        else if (storage_ == Storage::Items)
            delete items_;
    }

    void Variant::CopyFrom(const Variant& other)
    {
        type_ = other.type_;
        storage_ = other.storage_;
        size_ = other.size_;
        switch (storage_)
        {
        case Storage::Bytes:
        case Storage::Text:
            if (size_)
                std::memcpy(BytesFor(size_), other.Payload(), size_);
            break;
        case Storage::Items:
            items_ = new Array<Variant>();
            for (const Variant& v : *other.items_)
                items_->Push(v);
            break;
        default:
            std::memcpy(inline_, other.inline_, sizeof(inline_));
            break;
        }
    }

    void Variant::MoveFrom(Variant& other)
    {
        type_ = other.type_;
        storage_ = other.storage_;
        size_ = other.size_;
        std::memcpy(inline_, other.inline_, sizeof(inline_));
        other.storage_ = Storage::None;
        other.size_ = 0;
        other.type_ = TypeId();
    }

    u8* Variant::BytesFor(u32 size)
    {
        if (size <= kInlineBytes)
            return inline_;
        heap_ = static_cast<u8*>(HeapAllocator().Allocate(size, 16));
        return heap_;
    }

    Variant Variant::Bool(bool value)
    {
        Variant v;
        v.type_ = TypeOfPrimitive(Primitive::Bool);
        v.storage_ = Storage::Bool;
        v.b_ = value;
        return v;
    }

    Variant Variant::Int(TypeId type, i64 value)
    {
        Variant v;
        v.type_ = type;
        v.storage_ = Storage::Int;
        v.i_ = value;
        return v;
    }

    Variant Variant::UInt(TypeId type, u64 value)
    {
        Variant v;
        v.type_ = type;
        v.storage_ = Storage::UInt;
        v.u_ = value;
        return v;
    }

    Variant Variant::Real(TypeId type, f64 value)
    {
        Variant v;
        v.type_ = type;
        v.storage_ = Storage::Real;
        // A float holds what a float holds.
        v.d_ = type == TypeOfPrimitive(Primitive::Float) ? f64(f32(value)) : value;
        return v;
    }

    Variant Variant::Text(StringView value)
    {
        Variant v;
        v.type_ = TypeOfPrimitive(Primitive::String);
        v.storage_ = Storage::Text;
        v.size_ = u32(value.Size());
        if (v.size_)
            std::memcpy(v.BytesFor(v.size_), value.Data(), v.size_);
        return v;
    }

    Variant Variant::OfType(TypeId value)
    {
        Variant v;
        v.type_ = TypeOfPrimitive(Primitive::Type);
        v.storage_ = Storage::TypeValue;
        v.u_ = value.value;
        return v;
    }

    Variant Variant::Object(TypeId type, void* object)
    {
        Variant v;
        v.type_ = type;
        v.storage_ = Storage::Object;
        v.object_ = object;
        return v;
    }

    Variant Variant::Bytes(TypeId type, const void* bytes, u32 size)
    {
        Variant v;
        v.type_ = type;
        v.storage_ = Storage::Bytes;
        v.size_ = size;
        if (size)
            std::memcpy(v.BytesFor(size), bytes, size);
        return v;
    }

    Variant Variant::Enum(TypeId type, i64 value)
    {
        Variant v;
        v.type_ = type;
        v.storage_ = Storage::Int;
        v.i_ = value;
        return v;
    }

    Variant Variant::Items(TypeId arrayType, Array<Variant> items)
    {
        Variant v;
        v.type_ = arrayType;
        v.storage_ = Storage::Items;
        v.items_ = new Array<Variant>(Move(items));
        return v;
    }

    Variant Variant::FromMemory(TypeId type, const void* memory)
    {
        const TypeInfo* t = GetType(type);
        if (!t || !memory)
            return Variant();
        Primitive p = PrimitiveOf(type);
        auto bytes = [&](auto value) {
            std::memcpy(&value, memory, sizeof(value));
            return value;
        };
        switch (p)
        {
        case Primitive::Bool: return Bool(bytes(u8()) != 0);
        case Primitive::Int8: return Int(type, bytes(i8()));
        case Primitive::UInt8: return UInt(type, bytes(u8()));
        case Primitive::Int16: return Int(type, bytes(i16()));
        case Primitive::UInt16: return UInt(type, bytes(u16()));
        case Primitive::Int32: return Int(type, bytes(i32()));
        case Primitive::UInt32: return UInt(type, bytes(u32()));
        case Primitive::Int64: return Int(type, bytes(i64()));
        case Primitive::UInt64: return UInt(type, bytes(u64()));
        case Primitive::Float: return Real(type, bytes(f32()));
        case Primitive::Double: return Real(type, bytes(f64()));
        case Primitive::String: return Text(*static_cast<const String*>(memory));
        case Primitive::Type: return OfType(TypeId{bytes(u32())});
        default: break;
        }
        if (t->kind == TypeKind::Enum)
        {
            const TypeInfo* u = GetType(t->element);
            i64 value = 0;
            switch (u ? u->size : 4)
            {
            case 1: value = PrimitiveOf(t->element) == Primitive::Int8 ? i64(bytes(i8())) : i64(bytes(u8())); break;
            case 2: value = PrimitiveOf(t->element) == Primitive::Int16 ? i64(bytes(i16())) : i64(bytes(u16())); break;
            case 8: value = bytes(i64()); break;
            default: value = PrimitiveOf(t->element) == Primitive::UInt32 ? i64(bytes(u32())) : i64(bytes(i32())); break;
            }
            return Enum(type, value);
        }
        if (t->flags & TypePlain)
            return Bytes(type, memory, t->size);
        return Variant();
    }

    i64 Variant::AsInt() const
    {
        switch (storage_)
        {
        case Storage::Bool: return b_ ? 1 : 0;
        case Storage::Int: return i_;
        case Storage::UInt: return i64(u_);
        case Storage::Real: return i64(d_);
        default: return 0;
        }
    }

    u64 Variant::AsUInt() const
    {
        switch (storage_)
        {
        case Storage::Bool: return b_ ? 1 : 0;
        case Storage::Int: return u64(i_);
        case Storage::UInt: return u_;
        case Storage::Real: return u64(d_);
        default: return 0;
        }
    }

    f64 Variant::AsReal() const
    {
        switch (storage_)
        {
        case Storage::Bool: return b_ ? 1 : 0;
        case Storage::Int: return f64(i_);
        case Storage::UInt: return f64(u_);
        case Storage::Real: return d_;
        default: return 0;
        }
    }

    const void* Variant::Data() const
    {
        if (storage_ == Storage::Bytes)
            return Payload();
        return nullptr;
    }

    u32 Variant::DataSize() const { return storage_ == Storage::Bytes ? size_ : 0; }

    const Array<Variant>& Variant::AsItems() const
    {
        static const Array<Variant> kEmpty;
        return storage_ == Storage::Items ? *items_ : kEmpty;
    }

    bool Variant::ToMemory(TypeId type, void* memory) const
    {
        const TypeInfo* t = GetType(type);
        if (!t || !memory || type_ != type)
            return false;
        Primitive p = PrimitiveOf(type);
        auto store = [&](auto value) {
            std::memcpy(memory, &value, sizeof(value));
            return true;
        };
        switch (p)
        {
        case Primitive::Bool: return store(u8(AsBool() ? 1 : 0));
        case Primitive::Int8: return store(i8(i_));
        case Primitive::UInt8: return store(u8(u_));
        case Primitive::Int16: return store(i16(i_));
        case Primitive::UInt16: return store(u16(u_));
        case Primitive::Int32: return store(i32(i_));
        case Primitive::UInt32: return store(u32(u_));
        case Primitive::Int64: return store(i64(i_));
        case Primitive::UInt64: return store(u64(u_));
        case Primitive::Float: return store(f32(d_));
        case Primitive::Double: return store(f64(d_));
        case Primitive::String:
            *static_cast<String*>(memory) = String(AsText());
            return true;
        case Primitive::Type: return store(u32(u_));
        default: break;
        }
        if (t->kind == TypeKind::Enum)
        {
            switch (t->size)
            {
            case 1: return store(u8(i_));
            case 2: return store(u16(i_));
            case 8: return store(i_);
            default: return store(u32(i_));
            }
        }
        if (storage_ == Storage::Bytes && size_ == t->size)
        {
            std::memcpy(memory, Data(), size_);
            return true;
        }
        return false;
    }

    i32 Variant::ConversionCost(TypeId target) const
    {
        if (target == type_)
            return 0;
        Variant ignored;
        return ConvertTo(target, ignored) ? 1 : -1;
    }

    bool Variant::ConvertTo(TypeId target, Variant& out, bool explicitConversions) const
    {
        if (target == type_)
        {
            out = *this;
            return true;
        }
        const TypeInfo* to = GetType(target);
        if (!to)
            return false;
        Primitive pt = PrimitiveOf(target);
        Primitive pf = PrimitiveOf(type_);
        // Objects: null to any reference type, an object to its bases and interfaces (keeping its own type).
        if (storage_ == Storage::Object)
        {
            if (to->kind != TypeKind::Class && to->kind != TypeKind::Interface)
                return false;
            if (!object_ || IsAssignableFrom(target, type_))
            {
                out = *this;
                if (!object_)
                    out.type_ = target;
                return true;
            }
            return false;
        }
        bool isNumber = storage_ == Storage::Int || storage_ == Storage::UInt || storage_ == Storage::Real;
        const TypeInfo* from = GetType(type_);
        bool fromEnum = from && from->kind == TypeKind::Enum;
        if (IsNumber(pt) && isNumber && !fromEnum)
        {
            if (!Implicit(pf, pt) && !explicitConversions)
                return false;
            if (explicitConversions && !IsReal(pt) && storage_ != Storage::Real)
            {
                bool fits = storage_ == Storage::UInt ? (u_ <= u64(INT64_MAX) ? FitsSigned(i64(u_), pt) : pt == Primitive::UInt64)
                                                      : FitsSigned(i_, pt);
                if (!fits)
                    return false;
            }
            out = Number(pt, storage_ == Storage::Real, storage_ == Storage::UInt, i_, u_, d_);
            return true;
        }
        if (!explicitConversions)
            return false;
        // Explicit: enums and integers both ways, bools and numbers, text and numbers or enum names.
        if (to->kind == TypeKind::Enum && (isNumber || fromEnum))
        {
            out = Enum(target, AsInt());
            return true;
        }
        if (fromEnum && IsNumber(pt))
        {
            out = Number(pt, false, false, i_, u_, d_);
            return true;
        }
        if (pt == Primitive::Bool && (isNumber || storage_ == Storage::Bool))
        {
            out = Bool(AsInt() != 0);
            return true;
        }
        if (IsNumber(pt) && storage_ == Storage::Bool)
        {
            out = Number(pt, false, false, b_ ? 1 : 0, b_ ? 1 : 0, b_ ? 1 : 0);
            return true;
        }
        if (storage_ == Storage::Text)
        {
            StringView s = AsText().Trimmed();
            if (pt == Primitive::Bool)
            {
                if (s == "1" || s == "true" || s == "True")
                    return out = Bool(true), true;
                if (s == "0" || s == "false" || s == "False")
                    return out = Bool(false), true;
                return false;
            }
            if (to->kind == TypeKind::Enum)
            {
                for (const EnumValueInfo& e : EnumValues(target))
                    if (e.name == s)
                        return out = Enum(target, e.value), true;
                i64 v = 0;
                auto r = std::from_chars(s.Data(), s.Data() + s.Size(), v);
                if (r.ec != std::errc() || r.ptr != s.Data() + s.Size())
                    return false;
                return out = Enum(target, v), true;
            }
            if (IsReal(pt))
            {
                String copy(s);
                char* end = nullptr;
                f64 v = std::strtod(copy.CStr(), &end);
                if (s.Empty() || end != copy.CStr() + copy.Size())
                    return false;
                return out = Real(target, v), true;
            }
            if (IsSigned(pt))
            {
                i64 v = 0;
                auto r = std::from_chars(s.Data(), s.Data() + s.Size(), v);
                if (r.ec != std::errc() || r.ptr != s.Data() + s.Size() || !FitsSigned(v, pt))
                    return false;
                return out = Int(target, v), true;
            }
            if (IsUnsigned(pt))
            {
                u64 v = 0;
                auto r = std::from_chars(s.Data(), s.Data() + s.Size(), v);
                if (r.ec != std::errc() || r.ptr != s.Data() + s.Size() || (v > u64(INT64_MAX) ? pt != Primitive::UInt64 : !FitsSigned(i64(v), pt)))
                    return false;
                return out = UInt(target, v), true;
            }
        }
        if (pt == Primitive::String && storage_ != Storage::Items && storage_ != Storage::Bytes && storage_ != Storage::None)
        {
            switch (storage_)
            {
            case Storage::Bool: out = Text(b_ ? "1" : "0"); break;
            case Storage::Int:
                if (fromEnum)
                {
                    for (const EnumValueInfo& e : EnumValues(type_))
                        if (e.value == i_)
                            return out = Text(e.name), true;
                }
                out = Text(Format("{}", i_));
                break;
            case Storage::UInt: out = Text(Format("{}", u_)); break;
            case Storage::Real: out = Text(Format("{}", d_)); break;
            default: return false;
            }
            return true;
        }
        return false;
    }

    bool Variant::operator==(const Variant& other) const
    {
        if (type_ != other.type_ || storage_ != other.storage_)
            return false;
        switch (storage_)
        {
        case Storage::None: return true;
        case Storage::Bool: return b_ == other.b_;
        case Storage::Int: return i_ == other.i_;
        case Storage::UInt:
        case Storage::TypeValue: return u_ == other.u_;
        case Storage::Real: return d_ == other.d_;
        case Storage::Text: return AsText() == other.AsText();
        case Storage::Object: return object_ == other.object_;
        case Storage::Bytes: return size_ == other.size_ && std::memcmp(Data(), other.Data(), size_) == 0;
        case Storage::Items:
        {
            if (items_->Size() != other.items_->Size())
                return false;
            for (usize i = 0; i < items_->Size(); i++)
                if (!((*items_)[i] == (*other.items_)[i]))
                    return false;
            return true;
        }
        }
        return false;
    }
}
