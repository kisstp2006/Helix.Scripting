#include <Helix/Scripting/Types/Implementation/Registry.h>
#include <Helix/Scripting/Types/Invoke.h>

#include <Helix/Core/Memory/Allocator.h>
#include <Helix/Core/Strings/Format.h>

namespace helix::scripting
{
    namespace
    {
        constexpr usize kMaxArguments = 32;

        StringView TypeName(TypeId type)
        {
            const TypeInfo* t = GetType(type);
            return t ? t->fullName : StringView("<unknown type>");
        }

        // Stands in for an argument's value when only its type is known: a null object for reference types, else a
        // zero of the type (the conversion rules look at the type, not the value).
        Variant Probe(TypeId type)
        {
            const TypeInfo* t = GetType(type);
            if (!t)
                return Variant();
            if (t->kind == TypeKind::Class || t->kind == TypeKind::Interface)
                return Variant::Object(type, nullptr);
            if (type == TypeOfPrimitive(Primitive::String))
                return Variant::Text("");
            if (type == TypeOfPrimitive(Primitive::Bool))
                return Variant::Bool(false);
            if (t->kind == TypeKind::Enum)
                return Variant::Enum(type, 0);
            if (t->kind == TypeKind::Primitive && type != TypeOfPrimitive(Primitive::Type) && type != TypeOfPrimitive(Primitive::Void))
            {
                u64 zero = 0;
                return Variant::FromMemory(type, &zero);
            }
            return Variant::Bytes(type, nullptr, 0);
        }

        // C#'s better conversion target: `a` is better than `b` for an argument when a value of `a` converts
        // implicitly to `b` and not the other way (long is better than double for an int).
        bool BetterTarget(TypeId a, TypeId b)
        {
            if (a == b)
                return false;
            Variant pa = Probe(a), pb = Probe(b);
            return pa.ConversionCost(b) > 0 && pb.ConversionCost(a) < 0;
        }

        // `a` is better than `b` for the arguments: no worse for any argument, better for one (C#'s better function
        // member, for exact matches and implicit conversions).
        bool Better(TypeId owner, const MethodInfo& a, const MethodInfo& b, Span<const Variant> args)
        {
            Span<const ParameterInfo> pa = Parameters(owner, a), pb = Parameters(b.owner, b);
            bool better = false;
            for (usize i = 0; i < args.Size(); i++)
            {
                TypeId ta = pa[i].type, tb = pb[i].type, arg = args[i].Type();
                if (ta == tb)
                    continue;
                bool aExact = ta == arg, bExact = tb == arg;
                if ((aExact && !bExact) || (!bExact && BetterTarget(ta, tb)))
                    better = true;
                else if ((bExact && !aExact) || (!aExact && BetterTarget(tb, ta)))
                    return false;
            }
            return better;
        }

        // Every applicable method of the name on the type and its bases (the type's own first); the best one. A
        // method of a derived type hides a base method with the same parameters.
        const MethodInfo* Resolve(TypeId type, StringView name, Span<const Variant> args, String* error)
        {
            const MethodInfo* found[64];
            usize count = 0;
            bool named = false;
            for (TypeId at = type; const TypeInfo* t = GetType(at); at = t->base)
                for (const MethodInfo& m : Methods(at))
                {
                    if (m.name != name)
                        continue;
                    named = true;
                    Span<const ParameterInfo> ps = Parameters(at, m);
                    if (ps.Size() != args.Size())
                        continue;
                    bool applicable = true;
                    for (usize i = 0; i < ps.Size() && applicable; i++)
                        applicable = args[i].ConversionCost(ps[i].type) >= 0;
                    if (!applicable)
                        continue;
                    // Hidden by a method of a more derived type with the same parameter types.
                    bool hidden = false;
                    for (usize k = 0; k < count && !hidden; k++)
                    {
                        Span<const ParameterInfo> other = Parameters(found[k]->owner, *found[k]);
                        bool same = true;
                        for (usize i = 0; i < ps.Size() && same; i++)
                            same = other[i].type == ps[i].type;
                        hidden = same;
                    }
                    if (!hidden && count < 64)
                        found[count++] = &m;
                }
            const MethodInfo* best = nullptr;
            for (usize i = 0; i < count && !best; i++)
            {
                bool winner = true;
                for (usize k = 0; k < count && winner; k++)
                    winner = k == i || Better(found[i]->owner, *found[i], *found[k], args);
                if (winner)
                    best = found[i];
            }
            if (error)
            {
                if (!named)
                    *error = Format("{} has no method {}", TypeName(type), name);
                else if (count == 0)
                    *error = Format("no overload of {}.{} takes these {} arguments", TypeName(type), name, args.Size());
                else if (!best)
                    *error = Format("the call of {}.{} is ambiguous", TypeName(type), name);
            }
            return best;
        }
    }

    const MethodInfo* FindMethod(TypeId type, StringView name, Span<const TypeId> argumentTypes, String* error)
    {
        if (argumentTypes.Size() > kMaxArguments)
        {
            if (error)
                *error = "too many arguments";
            return nullptr;
        }
        Variant probes[kMaxArguments];
        for (usize i = 0; i < argumentTypes.Size(); i++)
            probes[i] = Probe(argumentTypes[i]);
        return Resolve(type, name, Span<const Variant>(probes, argumentTypes.Size()), error);
    }

    const MethodInfo* FindMethod(TypeId type, StringView name, Span<const Variant> arguments, String* error)
    {
        return Resolve(type, name, arguments, error);
    }

    bool Invoke(const MethodInfo& method, void* instance, Span<const Variant> arguments, Variant& result, String& error)
    {
        Span<const ParameterInfo> ps = Parameters(method.owner, method);
        if (ps.Size() != arguments.Size() || arguments.Size() > kMaxArguments)
        {
            error = Format("{}.{} takes {} arguments, not {}", TypeName(method.owner), method.name, ps.Size(), arguments.Size());
            return false;
        }
        if (!method.invoke)
        {
            error = Format("{}.{} cannot be called", TypeName(method.owner), method.name);
            return false;
        }
        if (!instance && !(method.flags & MemberStatic))
        {
            error = Format("{}.{} needs an instance", TypeName(method.owner), method.name);
            return false;
        }
        // Arguments already of their parameters' types go as they are; others are converted first.
        bool exact = true;
        for (usize i = 0; i < ps.Size(); i++)
            exact = exact && arguments[i].Type() == ps[i].type;
        result = Variant();
        if (exact)
            return method.invoke(method.context, instance, arguments, result, error);
        Variant converted[kMaxArguments];
        for (usize i = 0; i < ps.Size(); i++)
            if (!arguments[i].ConvertTo(ps[i].type, converted[i]))
            {
                error = Format("argument {} of {}.{}: {} does not convert to {}", i, TypeName(method.owner), method.name, TypeName(arguments[i].Type()),
                               TypeName(ps[i].type));
                return false;
            }
        return method.invoke(method.context, instance, Span<const Variant>(converted, ps.Size()), result, error);
    }

    bool Invoke(TypeId type, StringView name, void* instance, Span<const Variant> arguments, Variant& result, String& error)
    {
        const MethodInfo* m = Resolve(type, name, arguments, &error);
        return m && Invoke(*m, instance, arguments, result, error);
    }

    bool GetField(const FieldInfo& field, const void* instance, Variant& out, String& error)
    {
        if (!instance && !(field.flags & MemberStatic))
        {
            error = Format("{}.{} needs an instance", TypeName(field.owner), field.name);
            return false;
        }
        if (field.get)
            return field.get(field.context, instance, out, error);
        if (field.offset == kNoOffset)
        {
            error = Format("{}.{} cannot be read", TypeName(field.owner), field.name);
            return false;
        }
        out = Variant::FromMemory(field.type, static_cast<const u8*>(instance) + field.offset);
        if (out.IsVoid())
        {
            error = Format("{}.{}: values of {} are not read in place", TypeName(field.owner), field.name, TypeName(field.type));
            return false;
        }
        return true;
    }

    bool SetField(const FieldInfo& field, void* instance, const Variant& value, String& error, bool explicitConversions)
    {
        if (!instance && !(field.flags & MemberStatic))
        {
            error = Format("{}.{} needs an instance", TypeName(field.owner), field.name);
            return false;
        }
        if (field.flags & MemberReadOnly)
        {
            error = Format("{}.{} is read only", TypeName(field.owner), field.name);
            return false;
        }
        Variant v;
        if (!value.ConvertTo(field.type, v, explicitConversions))
        {
            error = Format("{}.{}: {} does not convert to {}", TypeName(field.owner), field.name, TypeName(value.Type()), TypeName(field.type));
            return false;
        }
        if (field.set)
            return field.set(field.context, instance, v, error);
        if (field.offset == kNoOffset || !v.ToMemory(field.type, static_cast<u8*>(instance) + field.offset))
        {
            error = Format("{}.{} cannot be written", TypeName(field.owner), field.name);
            return false;
        }
        return true;
    }

    bool GetProperty(const PropertyInfo& property, const void* instance, Variant& out, String& error)
    {
        const MethodInfo* getter = MethodAt(property.owner, property.getter);
        if (!getter)
        {
            error = Format("{}.{} has no getter", TypeName(property.owner), property.name);
            return false;
        }
        return Invoke(*getter, const_cast<void*>(instance), Span<const Variant>(), out, error);
    }

    bool SetProperty(const PropertyInfo& property, void* instance, const Variant& value, String& error)
    {
        const MethodInfo* setter = MethodAt(property.owner, property.setter);
        if (!setter)
        {
            error = Format("{}.{} is read only", TypeName(property.owner), property.name);
            return false;
        }
        Variant ignored;
        return Invoke(*setter, instance, Span<const Variant>(&value, 1), ignored, error);
    }

    bool GetMember(TypeId type, StringView name, const void* instance, Variant& out, String& error)
    {
        if (const FieldInfo* f = FindField(type, name))
            return GetField(*f, instance, out, error);
        if (const PropertyInfo* p = FindProperty(type, name))
            return GetProperty(*p, instance, out, error);
        error = Format("{} has no field or property {}", TypeName(type), name);
        return false;
    }

    bool SetMember(TypeId type, StringView name, void* instance, const Variant& value, String& error)
    {
        if (const FieldInfo* f = FindField(type, name))
            return SetField(*f, instance, value, error);
        if (const PropertyInfo* p = FindProperty(type, name))
            return SetProperty(*p, instance, value, error);
        error = Format("{} has no field or property {}", TypeName(type), name);
        return false;
    }

    namespace
    {
        bool EventCall(TypeId type, StringView event, u32 EventInfo::*which, const char* what, void* instance, Span<const Variant> args, String& error)
        {
            const EventInfo* e = FindEvent(type, event);
            if (!e)
            {
                error = Format("{} has no event {}", TypeName(type), event);
                return false;
            }
            const MethodInfo* m = MethodAt(e->owner, e->*which);
            if (!m)
            {
                error = Format("{}.{} cannot be {}", TypeName(type), event, what);
                return false;
            }
            Variant ignored;
            // Raise passes the event's own arguments through as they are.
            if (which == &EventInfo::raise)
                return m->invoke(m->context, instance, args, ignored, error);
            return Invoke(*m, instance, args, ignored, error);
        }
    }

    bool AddHandler(TypeId type, StringView event, void* instance, const Variant& handler, String& error)
    {
        return EventCall(type, event, &EventInfo::add, "added to", instance, Span<const Variant>(&handler, 1), error);
    }

    bool RemoveHandler(TypeId type, StringView event, void* instance, const Variant& handler, String& error)
    {
        return EventCall(type, event, &EventInfo::remove, "removed from", instance, Span<const Variant>(&handler, 1), error);
    }

    bool RaiseEvent(TypeId type, StringView event, void* instance, Span<const Variant> arguments, String& error)
    {
        return EventCall(type, event, &EventInfo::raise, "raised", instance, arguments, error);
    }

    void* CreateInstance(TypeId type)
    {
        const TypeInfo* t = GetType(type);
        if (!t || !t->construct || (t->flags & TypeAbstract) || t->size == 0)
            return nullptr;
        void* memory = HeapAllocator().Allocate(t->size, t->alignment ? t->alignment : 8);
        t->construct(memory);
        return memory;
    }

    void DestroyInstance(TypeId type, void* instance)
    {
        const TypeInfo* t = GetType(type);
        if (!t || !instance)
            return;
        if (t->destroy)
            t->destroy(instance);
        HeapAllocator().Free(instance, t->size, t->alignment ? t->alignment : 8);
    }
}
