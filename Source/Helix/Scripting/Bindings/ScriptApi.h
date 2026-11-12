#pragma once

#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Bindings/ObjectRef.h>
#include <Helix/Scripting/Types/TypeBuilder.h>

#include <bit>
#include <cstddef>
#include <type_traits>

// What the generated C++ script API stands on (Helix.Bindgen's API header of a module): value handles that hold an
// object's id and call the module's function table, the thunks C# calls, so both languages take one path and get one
// set of checks. A failed call is logged with the member's name and answers the default value.
namespace helix::scripting
{
    // The base of the value handles: the object's id (0 is null). Copies name the same object.
    class ScriptHandle
    {
    public:
        u64 HxId() const { return id_; }

    protected:
        u64 id_ = 0;
    };

    template <class T>
    concept ApiHandle = std::is_base_of_v<ScriptHandle, T>;

    // What a generic member answers for T: the handle of an API class, or for a C++ script class an ObjectRef (its
    // object by id, null once destroyed).
    template <class T, class Root>
    using ApiRef = std::conditional_t<ApiHandle<T>, T, ObjectRef<Root, T>>;

    template <class T, class Root>
    ApiRef<T, Root> ApiAs(u64 id)
    {
        if constexpr (ApiHandle<T>)
            return T::HxFromId(id);
        else
            return ObjectRef<Root, T>::FromId(id);
    }

    // The registered type of T: an API class's, or a C++ script class's (its module's registration).
    template <class T>
    TypeId ApiTypeOf()
    {
        if constexpr (ApiHandle<T>)
            return T::HxType();
        else
            return TypeOf<T>();
    }

    // A string for a call: its UTF-16 copy.
    class ApiString16
    {
    public:
        explicit ApiString16(StringView text);
        const char16_t* Data() const { return data_.Data(); }
        i32 Size() const { return i32(data_.Size()); }

    private:
        Array<char16_t> data_;
    };

    // A string a call handed out (UTF-8, the thread's buffer) as a String of its own.
    inline String ApiString(const InteropString& s) { return String(StringView(s.data, usize(s.size > 0 ? s.size : 0))); }

    // The function of the module's table at the index: the table is looked up until the module registered.
    template <const char* Module>
    const void* ApiFunction(u32 index)
    {
        static Span<const void* const> table;
        if (index >= table.Size())
            table = NativeFunctions(StringView(Module));
        return index < table.Size() ? table[index] : nullptr;
    }

    // The registered type of an API class by its full name (looked up once).
    inline TypeId ApiType(StringView fullName, TypeId& cache)
    {
        if (!cache.IsValid())
            cache = FindType(fullName);
        return cache;
    }

    // Answers false (logged) when a function is missing or a call failed.
    inline bool ApiCheck(const void* function, StringView what)
    {
        if (function)
            return true;
        ReportBindingFailure(what, InteropNotLoaded);
        return false;
    }

    inline bool ApiStatus(i32 status, StringView what)
    {
        if (status == InteropOk)
            return true;
        ReportBindingFailure(what, status);
        return false;
    }
}
