#pragma once

#include <Helix/Scripting/Bindings/Interop.h>

#include <cstddef>
#include <type_traits>

// A reference to a native object by its id, with the reference's null semantics in C++: a destroyed object is null
// (`if (ref)` false, `ref == nullptr` true), two references are equal when both are null or both name the same
// object. It holds no pointer, so it never dangles; Get answers the object while it lives. The value handles of the
// C++ script API are built on it.
namespace helix::scripting
{
    template <class Root, class T = Root>
    class ObjectRef
    {
    public:
        ObjectRef() = default;
        ObjectRef(std::nullptr_t) {}
        explicit ObjectRef(const T* object) : id_(object ? ObjectHandles<Root>::ToId(object) : 0) {}

        static ObjectRef FromId(u64 id)
        {
            ObjectRef r;
            r.id_ = id;
            return r;
        }

        // A reference of a base class (or the root).
        template <class U>
            requires(std::is_base_of_v<U, T> && !std::is_same_v<U, T>)
        operator ObjectRef<Root, U>() const
        {
            return ObjectRef<Root, U>::FromId(id_);
        }

        u64 Id() const { return id_; }
        // The object while it lives, null once it is destroyed.
        T* Get() const { return id_ ? static_cast<T*>(ObjectHandles<Root>::FromId(id_)) : nullptr; }
        bool IsAlive() const { return Get() != nullptr; }
        explicit operator bool() const { return IsAlive(); }

    private:
        u64 id_ = 0;
    };

    template <class Root, class T>
    bool operator==(const ObjectRef<Root, T>& a, std::nullptr_t)
    {
        return !a.IsAlive();
    }

    template <class Root, class A, class B>
    bool operator==(const ObjectRef<Root, A>& a, const ObjectRef<Root, B>& b)
    {
        bool aliveA = a.IsAlive(), aliveB = b.IsAlive();
        if (!aliveA || !aliveB)
            return aliveA == aliveB;
        return a.Id() == b.Id();
    }
}
