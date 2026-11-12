#pragma once

#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Types/Variant.h>

#include <type_traits>

// Values between C++ and Variants, for the reflection invokers Helix.Bindgen writes: primitives, enums, plain structs,
// strings, objects (with their dynamic type through the module's ObjectHandles) and arrays of those.
namespace helix::scripting
{
    namespace detail
    {
        template <class T>
        struct IsArray : std::false_type
        {
        };
        template <class T>
        struct IsArray<Array<T>> : std::true_type
        {
            using Item = T;
        };
        template <class T>
        struct IsArray<Span<T>> : std::true_type
        {
            using Item = std::remove_const_t<T>;
        };
    }

    // The registered type of a value type; for object pointers, the type of the object.
    template <class T>
    TypeId TypeOfValue()
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::is_pointer_v<U>)
            return TypeOf<std::remove_cv_t<std::remove_pointer_t<U>>>();
        else
            return TypeOf<U>();
    }

    // An object: the root's handles know its dynamic type.
    template <class Root, class T>
    Variant ObjectToVariant(T* object)
    {
        TypeId type = object ? ObjectHandles<Root>::TypeOf(object) : TypeOf<T>();
        return Variant::Object(type.IsValid() ? type : TypeOf<T>(), const_cast<std::remove_const_t<T>*>(object));
    }

    template <class T>
    Variant ToVariant(const T& value)
    {
        using U = std::remove_cvref_t<T>;
        if constexpr (detail::IsArray<U>::value)
        {
            using Item = typename detail::IsArray<U>::Item;
            Array<Variant> items;
            for (const Item& item : value)
                items.Push(ToVariant(item));
            return Variant::Items(ArrayOf(TypeOfValue<Item>()), Move(items));
        }
        else
            return Variant::From(value);
    }

    template <class T>
    bool FromVariant(const Variant& value, T& out)
    {
        using U = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<U, StringView>)
            return false;
        else if constexpr (detail::IsArray<U>::value)
        {
            using Item = typename detail::IsArray<U>::Item;
            if constexpr (std::is_same_v<U, Array<Item>>)
            {
                out.Clear();
                for (const Variant& v : value.AsItems())
                {
                    Item item{};
                    if (!FromVariant(v, item))
                        return false;
                    out.Push(Move(item));
                }
                return true;
            }
            else
                return false;
        }
        else
            return value.Get(out);
    }
}
