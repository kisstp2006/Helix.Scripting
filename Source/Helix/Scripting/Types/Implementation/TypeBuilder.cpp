#include <Helix/Scripting/Types/Implementation/Registry.h>

#include <Helix/Core/Memory/Allocator.h>
#include <Helix/Core/Strings/Format.h>

#include <cstring>

namespace helix::scripting
{
    using namespace detail;

    TypeBuilder::~TypeBuilder()
    {
        Finish();
        for (char* n : names_)
            HeapAllocator().Free(n, 0, 1);
    }

    StringView TypeBuilder::Keep(StringView name)
    {
        char* copy = static_cast<char*>(HeapAllocator().Allocate(name.Size() + 1, 1));
        if (!name.Empty())
            std::memcpy(copy, name.Data(), name.Size());
        copy[name.Size()] = 0;
        names_.Push(copy);
        return StringView(copy, name.Size());
    }

    TypeBuilder& TypeBuilder::Size(u32 size, u32 alignment)
    {
        size_ = size, alignment_ = alignment, sized_ = true;
        return *this;
    }

    TypeBuilder& TypeBuilder::Lifetime(Constructor construct, Destructor destroy)
    {
        construct_ = construct, destroy_ = destroy;
        return *this;
    }

    TypeBuilder& TypeBuilder::Flags(u32 flags)
    {
        flags_ |= flags, flagged_ = true;
        return *this;
    }

    TypeBuilder& TypeBuilder::User(u64 user)
    {
        user_ = user, hasUser_ = true;
        return *this;
    }

    TypeBuilder& TypeBuilder::Interface(TypeId type)
    {
        interfaces_.Push(type);
        return *this;
    }

    TypeBuilder& TypeBuilder::Underlying(TypeId type)
    {
        underlying_ = type;
        return *this;
    }

    TypeBuilder& TypeBuilder::Field(StringView name, TypeId type, u32 offset, u32 flags)
    {
        FieldInfo f;
        f.name = Keep(name);
        f.owner = type_;
        f.type = type;
        f.flags = flags;
        f.offset = offset;
        fields_.Push(f);
        last_ = Target::Field;
        return *this;
    }

    TypeBuilder& TypeBuilder::Field(StringView name, TypeId type, FieldGetter get, FieldSetter set, const void* context, u32 flags)
    {
        FieldInfo f;
        f.name = Keep(name);
        f.owner = type_;
        f.type = type;
        f.flags = flags | (set ? 0u : u32(MemberReadOnly));
        f.get = get;
        f.set = set;
        f.context = context;
        fields_.Push(f);
        last_ = Target::Field;
        return *this;
    }

    TypeBuilder& TypeBuilder::Method(StringView name, TypeId returnType, std::initializer_list<ParameterInfo> parameters, Invoker invoke,
                                     const void* context, u32 flags)
    {
        return Method(name, returnType, Span<const ParameterInfo>(parameters.begin(), parameters.size()), invoke, context, flags);
    }

    TypeBuilder& TypeBuilder::Method(StringView name, TypeId returnType, Span<const ParameterInfo> parameters, Invoker invoke, const void* context,
                                     u32 flags)
    {
        MethodInfo m;
        m.name = Keep(name);
        m.owner = type_;
        m.returnType = returnType.IsValid() ? returnType : TypeOfPrimitive(Primitive::Void);
        m.flags = flags;
        m.invoke = invoke;
        m.context = context;
        Array<ParameterInfo> list;
        for (const ParameterInfo& p : parameters)
        {
            ParameterInfo copy = p;
            copy.name = Keep(p.name);
            list.Push(copy);
        }
        parameters_.Push(Move(list));
        methods_.Push(m);
        last_ = Target::Method;
        return *this;
    }

    TypeBuilder& TypeBuilder::Property(StringView name, TypeId type, Invoker getter, Invoker setter, const void* context, u32 flags)
    {
        PropertyInfo p;
        p.name = Keep(name);
        p.owner = type_;
        p.type = type;
        p.flags = flags | (setter ? 0u : u32(MemberReadOnly));
        // Getter and setter as methods; their indices are local until Finish.
        if (getter)
        {
            p.getter = u32(methods_.Size());
            Method(Format("get_{}", name), type, {}, getter, context, flags);
        }
        if (setter)
        {
            p.setter = u32(methods_.Size());
            Method(Format("set_{}", name), TypeId(), {ParameterInfo{"value", type, ParameterNone}}, setter, context, flags);
        }
        properties_.Push(p);
        last_ = Target::Property;
        return *this;
    }

    TypeBuilder& TypeBuilder::Event(StringView name, TypeId handlerType, Invoker add, Invoker remove, Invoker raise, const void* context, u32 flags)
    {
        EventInfo e;
        e.name = Keep(name);
        e.owner = type_;
        e.handlerType = handlerType;
        e.flags = flags;
        const ParameterInfo handler{"handler", handlerType, ParameterNone};
        if (add)
        {
            e.add = u32(methods_.Size());
            Method(Format("add_{}", name), TypeId(), {handler}, add, context, flags);
        }
        if (remove)
        {
            e.remove = u32(methods_.Size());
            Method(Format("remove_{}", name), TypeId(), {handler}, remove, context, flags);
        }
        if (raise)
        {
            e.raise = u32(methods_.Size());
            Method(Format("raise_{}", name), TypeId(), {}, raise, context, flags);
        }
        events_.Push(e);
        last_ = Target::Event;
        return *this;
    }

    TypeBuilder& TypeBuilder::EnumValue(StringView name, i64 value)
    {
        enumValues_.Push(EnumValueInfo{Keep(name), value});
        return *this;
    }

    TypeBuilder& TypeBuilder::Attribute(TypeId type, std::initializer_list<Variant> arguments)
    {
        return Attribute(type, Span<const Variant>(arguments.begin(), arguments.size()));
    }

    TypeBuilder& TypeBuilder::Attribute(TypeId type, Span<const Variant> arguments)
    {
        Staged s;
        s.target = last_;
        switch (last_)
        {
        case Target::Field: s.index = u32(fields_.Size() - 1); break;
        case Target::Method: s.index = u32(methods_.Size() - 1); break;
        case Target::Property: s.index = u32(properties_.Size() - 1); break;
        case Target::Event: s.index = u32(events_.Size() - 1); break;
        default: s.index = 0; break;
        }
        s.type = type;
        for (const Variant& v : arguments)
            s.arguments.Push(v);
        attributes_.Push(Move(s));
        return *this;
    }

    TypeId TypeBuilder::Finish()
    {
        if (done_)
            return type_;
        done_ = true;
        Slot* slot = SlotOf(type_);
        if (!slot)
            return TypeId();
        ModuleData* m = ModuleOf(slot->info.module);
        if (!m)
            return TypeId();
        TypeInfo& t = slot->info;
        if (sized_)
            t.size = size_, t.alignment = alignment_;
        if (flagged_)
            t.flags |= flags_;
        if (construct_ || destroy_)
            t.construct = construct_, t.destroy = destroy_;
        if (hasUser_)
            t.user = user_;
        if (underlying_.IsValid())
            t.element = underlying_;
        if (t.kind == TypeKind::Enum && !t.element.IsValid())
            t.element = TypeOfPrimitive(Primitive::Int32);
        if (t.kind == TypeKind::Enum && !sized_)
        {
            const TypeInfo* u = GetType(t.element);
            t.size = u ? u->size : 4;
            t.alignment = t.size;
        }

        auto range = [](usize first, usize count) { return Range{u32(first), u32(count)}; };
        u32 methodBase = u32(m->methods.Size());
        t.interfaces = range(m->interfaces.Size(), interfaces_.Size());
        for (TypeId i : interfaces_)
            m->interfaces.Push(i);
        // Members, their names copied into the module; attribute ranges filled below.
        t.fields = range(m->fields.Size(), fields_.Size());
        for (FieldInfo f : fields_)
        {
            f.name = m->Intern(f.name);
            m->fields.Push(f);
        }
        t.methods = range(m->methods.Size(), methods_.Size());
        for (usize i = 0; i < methods_.Size(); i++)
        {
            MethodInfo mi = methods_[i];
            mi.name = m->Intern(mi.name);
            mi.parameters = range(m->parameters.Size(), parameters_[i].Size());
            for (ParameterInfo p : parameters_[i])
            {
                p.name = m->Intern(p.name);
                m->parameters.Push(p);
            }
            m->methods.Push(mi);
        }
        t.properties = range(m->properties.Size(), properties_.Size());
        for (PropertyInfo p : properties_)
        {
            p.name = m->Intern(p.name);
            if (p.getter != kNoMember)
                p.getter += methodBase;
            if (p.setter != kNoMember)
                p.setter += methodBase;
            m->properties.Push(p);
        }
        t.events = range(m->events.Size(), events_.Size());
        for (EventInfo e : events_)
        {
            e.name = m->Intern(e.name);
            for (u32* index : {&e.add, &e.remove, &e.raise})
                if (*index != kNoMember)
                    *index += methodBase;
            m->events.Push(e);
        }
        t.enumValues = range(m->enumValues.Size(), enumValues_.Size());
        for (EnumValueInfo v : enumValues_)
        {
            v.name = m->Intern(v.name);
            m->enumValues.Push(v);
        }

        // Attributes grouped by what they belong to, so each owner's are one range.
        auto attach = [&](Target target, u32 index) {
            Range r{u32(m->attributes.Size()), 0};
            for (const Staged& s : attributes_)
            {
                if (s.target != target || s.index != index)
                    continue;
                AttributeInfo a;
                a.type = s.type;
                a.args = Range{u32(m->arguments.Size()), u32(s.arguments.Size())};
                for (const Variant& v : s.arguments)
                    m->arguments.Push(v);
                m->attributes.Push(a);
                r.count++;
            }
            return r;
        };
        t.attributes = attach(Target::Type, 0);
        for (u32 i = 0; i < t.fields.count; i++)
            m->fields[t.fields.first + i].attributes = attach(Target::Field, i);
        for (u32 i = 0; i < t.methods.count; i++)
            m->methods[t.methods.first + i].attributes = attach(Target::Method, i);
        for (u32 i = 0; i < t.properties.count; i++)
            m->properties[t.properties.first + i].attributes = attach(Target::Property, i);
        for (u32 i = 0; i < t.events.count; i++)
            m->events[t.events.first + i].attributes = attach(Target::Event, i);
        return type_;
    }
}
