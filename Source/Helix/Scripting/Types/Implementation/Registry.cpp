#include <Helix/Scripting/Types/Implementation/Registry.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Memory/Allocator.h>
#include <Helix/Core/Strings/Format.h>

#include <cstring>

namespace helix::scripting
{
    namespace detail
    {
        namespace
        {
            constexpr u32 kSlotBits = 20;
            constexpr u32 kGenerationMask = (1u << (32 - kSlotBits)) - 1;
            constexpr usize kBlockSize = 4096;

            u64 NameHash(StringView name) { return Fnv1a64(name.Data(), name.Size()); }

            struct PrimitiveDesc
            {
                const char* name;
                u32 size;
                u32 flags;
            };

            // In the order of Primitive (from Void = 1).
            constexpr PrimitiveDesc kPrimitives[] = {
                {"Void", 0, TypeNone},
                {"Boolean", 1, TypePlain},
                {"SByte", 1, TypePlain},
                {"Byte", 1, TypePlain},
                {"Int16", 2, TypePlain},
                {"UInt16", 2, TypePlain},
                {"Int32", 4, TypePlain},
                {"UInt32", 4, TypePlain},
                {"Int64", 8, TypePlain},
                {"UInt64", 8, TypePlain},
                {"Single", 4, TypePlain},
                {"Double", 8, TypePlain},
                {"String", u32(sizeof(String)), TypeNone},
                {"Type", 4, TypePlain},
            };
        }

        ModuleData::~ModuleData()
        {
            for (char* b : blocks)
                HeapAllocator().Free(b, 0, 1);
        }

        StringView ModuleData::Intern(StringView text)
        {
            usize n = text.Size() + 1;
            if (n > left)
            {
                usize size = n > kBlockSize ? n : kBlockSize;
                cursor = static_cast<char*>(HeapAllocator().Allocate(size, 1));
                blocks.Push(cursor);
                left = size;
            }
            char* at = cursor;
            if (!text.Empty())
                std::memcpy(at, text.Data(), text.Size());
            at[text.Size()] = 0;
            cursor += n;
            left -= n;
            return StringView(at, text.Size());
        }

        Registry::Registry()
        {
            slots.Emplace();
            ModuleData* m = new ModuleData();
            m->name = String("System");
            m->info = {m->name, ModuleKind::Builtin};
            modules.Push(m);
            builtin = ModuleId{1};
            for (const PrimitiveDesc& p : kPrimitives)
            {
                u32 slot = u32(slots.Size());
                Slot& s = slots.Emplace();
                s.used = true;
                s.generation = 1;
                TypeInfo& t = s.info;
                t.name = m->Intern(p.name);
                t.nameSpace = m->Intern("System");
                t.fullName = m->Intern(Format("System.{}", p.name));
                t.module = builtin;
                t.kind = TypeKind::Primitive;
                t.flags = p.flags;
                t.size = p.size;
                t.alignment = p.size > 8 ? 8 : (p.size ? p.size : 1);
                s.hash = NameHash(t.fullName);
                byName.Set(s.hash, slot);
                m->types.Push(slot);
            }
        }

        Registry::~Registry()
        {
            for (ModuleData* m : modules)
                delete m;
        }

        Registry& R()
        {
            static Registry r;
            return r;
        }

        TypeId MakeId(u32 slot, u32 generation) { return TypeId{slot | (generation << kSlotBits)}; }

        Slot* SlotOf(TypeId type)
        {
            Registry& r = R();
            u32 slot = type.Slot();
            if (!type.IsValid() || slot >= r.slots.Size())
                return nullptr;
            Slot& s = r.slots[slot];
            if (!s.used || s.generation != (type.value >> kSlotBits))
                return nullptr;
            return &s;
        }

        ModuleData* ModuleOf(ModuleId module)
        {
            Registry& r = R();
            if (!module.IsValid() || module.value > r.modules.Size())
                return nullptr;
            return r.modules[module.value - 1];
        }

        void EnsureOrders()
        {
            Registry& r = R();
            if (!r.ordersDirty.load(std::memory_order_acquire))
                return;
            platform::ScopedLock lock(r.ordersMutex);
            if (!r.ordersDirty.load(std::memory_order_relaxed))
                return;
            // Children by base, then depth first numbers from every root.
            u32 n = u32(r.slots.Size());
            Array<u32> firstChild, nextSibling;
            firstChild.Resize(n, 0u);
            nextSibling.Resize(n, 0u);
            for (u32 i = n; i-- > 1;)
            {
                Slot& s = r.slots[i];
                if (!s.used)
                    continue;
                Slot* b = SlotOf(s.info.base);
                if (!b)
                    continue;
                u32 parent = s.info.base.Slot();
                nextSibling[i] = firstChild[parent];
                firstChild[parent] = i;
            }
            u32 counter = 0;
            Array<u32> stack;
            for (u32 i = 1; i < n; i++)
            {
                Slot& s = r.slots[i];
                if (!s.used || SlotOf(s.info.base))
                    continue;
                // Pre order: the number on the way down, the end on the way up (a node seen twice).
                stack.Push(i << 1);
                while (!stack.Empty())
                {
                    u32 e = stack.Back();
                    stack.Pop();
                    u32 at = e >> 1;
                    if (e & 1)
                    {
                        r.slots[at].info.orderEnd = counter;
                        continue;
                    }
                    r.slots[at].info.order = counter++;
                    stack.Push((at << 1) | 1);
                    for (u32 c = firstChild[at]; c; c = nextSibling[c])
                        stack.Push(c << 1);
                }
            }
            r.ordersDirty.store(false, std::memory_order_release);
        }
    }

    using namespace detail;

    // ------------------------------------------------------------------------------------------------- providers

    namespace
    {
        struct Providers
        {
            Array<void (*)()> list = Array<void (*)()>();
            usize ran = 0;
            bool running = false;
        };

        Providers& P()
        {
            static Providers p;
            return p;
        }

        // Runs the providers added since the last lookup (not again while one of them registers types).
        void RunProviders()
        {
            Providers& p = P();
            if (p.running || p.ran == p.list.Size())
                return;
            p.running = true;
            while (p.ran < p.list.Size())
                p.list[p.ran++]();
            p.running = false;
        }
    }

    void AddTypeProvider(void (*provide)())
    {
        if (provide)
            P().list.Push(provide);
    }

    // ------------------------------------------------------------------------------------------------- modules

    ModuleId RegisterModule(StringView name, ModuleKind kind)
    {
        Registry& r = R();
        for (ModuleData* m : r.modules)
            if (m && m->name == name)
                return ModuleId();
        ModuleData* m = new ModuleData();
        m->name = String(name);
        m->info = {m->name, kind};
        r.modules.Push(m);
        return ModuleId{u32(r.modules.Size())};
    }

    ModuleId FindModule(StringView name)
    {
        RunProviders();
        Registry& r = R();
        for (u32 i = 0; i < r.modules.Size(); i++)
            if (r.modules[i] && r.modules[i]->name == name)
                return ModuleId{i + 1};
        return ModuleId();
    }

    const ModuleInfo* GetModule(ModuleId module)
    {
        ModuleData* m = ModuleOf(module);
        return m ? &m->info : nullptr;
    }

    namespace
    {
        void FreeSlot(u32 slot)
        {
            Registry& r = R();
            Slot& s = r.slots[slot];
            // Out of the name chain.
            if (u32* first = r.byName.Find(s.hash))
            {
                if (*first == slot)
                {
                    if (s.next)
                        *first = s.next;
                    else
                        r.byName.Remove(s.hash);
                }
                else
                    for (u32 at = *first; at; at = r.slots[at].next)
                        if (r.slots[at].next == slot)
                        {
                            r.slots[at].next = s.next;
                            break;
                        }
            }
            s.used = false;
            s.next = 0;
            s.generation = (s.generation + 1) & kGenerationMask;
            if (s.generation == 0)
                s.generation = 1;
            s.info = TypeInfo();
            r.freeSlots.Push(slot);
        }
    }

    void UnloadModule(ModuleId module)
    {
        Registry& r = R();
        ModuleData* m = ModuleOf(module);
        if (!m || module == r.builtin)
            return;
        for (u32 slot : m->types)
        {
            if (u32* array = r.arrays.Find(slot))
            {
                u32 a = *array;
                r.arrays.Remove(slot);
                FreeSlot(a);
            }
            FreeSlot(slot);
        }
        delete m;
        r.modules[module.value - 1] = nullptr;
        r.ordersDirty.store(true, std::memory_order_release);
    }

    // ------------------------------------------------------------------------------------------------- types

    namespace
    {
        TypeId FindRegistered(StringView fullName);
    }

    TypeId DeclareType(ModuleId module, StringView nameSpace, StringView name, TypeKind kind, TypeId base, u32 flags)
    {
        Registry& r = R();
        ModuleData* m = ModuleOf(module);
        if (!m || name.Empty() || (base.IsValid() && !SlotOf(base)))
            return TypeId();
        String full = nameSpace.Empty() ? String(name) : Format("{}.{}", nameSpace, name);
        if (FindRegistered(full).IsValid())
            return TypeId();
        u32 slot;
        if (!r.freeSlots.Empty())
        {
            slot = r.freeSlots.Back();
            r.freeSlots.Pop();
        }
        else
        {
            slot = u32(r.slots.Size());
            if (slot >= (1u << 20))
                return TypeId();
            r.slots.Emplace().generation = 1;
        }
        Slot& s = r.slots[slot];
        s.used = true;
        TypeInfo& t = s.info;
        t = TypeInfo();
        t.fullName = m->Intern(full);
        t.nameSpace = t.fullName.Substr(0, nameSpace.Size());
        t.name = t.fullName.Substr(nameSpace.Empty() ? 0 : nameSpace.Size() + 1);
        t.module = module;
        t.kind = kind;
        t.flags = flags;
        t.base = base;
        if (kind == TypeKind::Enum)
            t.flags |= TypePlain;
        s.hash = NameHash(t.fullName);
        if (u32* first = r.byName.Find(s.hash))
        {
            s.next = *first;
            *first = slot;
        }
        else
        {
            s.next = 0;
            r.byName.Set(s.hash, slot);
        }
        m->types.Push(slot);
        r.ordersDirty.store(true, std::memory_order_release);
        return MakeId(slot, s.generation);
    }

    const TypeInfo* GetType(TypeId type)
    {
        Slot* s = SlotOf(type);
        return s ? &s->info : nullptr;
    }

    TypeId TypeOfPrimitive(Primitive primitive)
    {
        if (primitive < Primitive::Void || primitive >= Primitive::Count)
            return TypeId();
        return MakeId(u32(primitive), 1);
    }

    namespace
    {
        // By name, without asking the providers (registration itself checks names this way).
        TypeId FindRegistered(StringView fullName)
        {
            Registry& r = R();
            const u32* first = r.byName.Find(NameHash(fullName));
            for (u32 at = first ? *first : 0; at; at = r.slots[at].next)
                if (r.slots[at].used && r.slots[at].info.fullName == fullName)
                    return MakeId(at, r.slots[at].generation);
            return TypeId();
        }
    }

    TypeId FindType(StringView fullName)
    {
        RunProviders();
        return FindRegistered(fullName);
    }

    TypeId ArrayOf(TypeId element)
    {
        Registry& r = R();
        Slot* e = SlotOf(element);
        if (!e)
            return TypeId();
        if (const u32* a = r.arrays.Find(element.Slot()))
            return MakeId(*a, r.slots[*a].generation);
        ModuleData* m = ModuleOf(r.builtin);
        String name = Format("{}[]", e->info.name);
        // Array types are named after their element and live in the builtin module, keyed by the element's slot.
        u32 slot;
        if (!r.freeSlots.Empty())
        {
            slot = r.freeSlots.Back();
            r.freeSlots.Pop();
        }
        else
        {
            slot = u32(r.slots.Size());
            r.slots.Emplace().generation = 1;
        }
        Slot& s = r.slots[slot];
        Slot* elementSlot = SlotOf(element);
        s.used = true;
        TypeInfo& t = s.info;
        t = TypeInfo();
        t.fullName = m->Intern(Format("{}[]", elementSlot->info.fullName));
        t.nameSpace = t.fullName.Substr(0, elementSlot->info.nameSpace.Size());
        t.name = m->Intern(name);
        t.module = r.builtin;
        t.kind = TypeKind::Array;
        t.element = element;
        // Not chained by name: an array type is found through its element.
        s.hash = 0;
        s.next = 0;
        r.arrays.Set(element.Slot(), slot);
        return MakeId(slot, s.generation);
    }

    void TypesOfModule(ModuleId module, Array<TypeId>& out)
    {
        RunProviders();
        out.Clear();
        ModuleData* m = ModuleOf(module);
        if (!m)
            return;
        Registry& r = R();
        for (u32 slot : m->types)
            out.Push(MakeId(slot, r.slots[slot].generation));
    }

    bool IsAssignableFrom(TypeId target, TypeId source)
    {
        Slot* t = SlotOf(target);
        Slot* s = SlotOf(source);
        if (!t || !s)
            return false;
        if (target == source)
            return true;
        EnsureOrders();
        if (s->info.order >= t->info.order && s->info.order < t->info.orderEnd)
            return true;
        if (t->info.kind != TypeKind::Interface)
            return false;
        // The interfaces of the type and of its bases, and the interfaces they derive from.
        for (TypeId at = source; Slot* a = SlotOf(at); at = a->info.base)
            for (TypeId i : Interfaces(at))
                if (IsAssignableFrom(target, i))
                    return true;
        return false;
    }

    // ------------------------------------------------------------------------------------------------- members

    namespace
    {
        template <class T>
        Span<const T> RangeOf(TypeId type, Range range, Array<T> ModuleData::*array)
        {
            Slot* s = SlotOf(type);
            if (!s || range.count == 0)
                return Span<const T>();
            ModuleData* m = ModuleOf(s->info.module);
            if (!m)
                return Span<const T>();
            const Array<T>& a = m->*array;
            return Span<const T>(a.Data() + range.first, range.count);
        }

        template <class T>
        Span<const T> Own(TypeId type, Range TypeInfo::*range, Array<T> ModuleData::*array)
        {
            Slot* s = SlotOf(type);
            return s ? RangeOf(type, s->info.*range, array) : Span<const T>();
        }
    }

    Span<const TypeId> Interfaces(TypeId type) { return Own(type, &TypeInfo::interfaces, &ModuleData::interfaces); }
    Span<const FieldInfo> Fields(TypeId type) { return Own(type, &TypeInfo::fields, &ModuleData::fields); }
    Span<const PropertyInfo> Properties(TypeId type) { return Own(type, &TypeInfo::properties, &ModuleData::properties); }
    Span<const MethodInfo> Methods(TypeId type) { return Own(type, &TypeInfo::methods, &ModuleData::methods); }
    Span<const EventInfo> Events(TypeId type) { return Own(type, &TypeInfo::events, &ModuleData::events); }
    Span<const EnumValueInfo> EnumValues(TypeId type) { return Own(type, &TypeInfo::enumValues, &ModuleData::enumValues); }
    Span<const AttributeInfo> Attributes(TypeId type) { return Own(type, &TypeInfo::attributes, &ModuleData::attributes); }
    Span<const ParameterInfo> Parameters(TypeId owner, const MethodInfo& method) { return RangeOf(owner, method.parameters, &ModuleData::parameters); }
    Span<const AttributeInfo> Attributes(TypeId owner, Range attributes) { return RangeOf(owner, attributes, &ModuleData::attributes); }
    Span<const Variant> AttributeArguments(TypeId owner, const AttributeInfo& attribute) { return RangeOf(owner, attribute.args, &ModuleData::arguments); }

    const FieldInfo* FindField(TypeId type, StringView name)
    {
        for (TypeId at = type; Slot* s = SlotOf(at); at = s->info.base)
            for (const FieldInfo& f : Fields(at))
                if (f.name == name)
                    return &f;
        return nullptr;
    }

    const PropertyInfo* FindProperty(TypeId type, StringView name)
    {
        for (TypeId at = type; Slot* s = SlotOf(at); at = s->info.base)
            for (const PropertyInfo& p : Properties(at))
                if (p.name == name)
                    return &p;
        return nullptr;
    }

    const EventInfo* FindEvent(TypeId type, StringView name)
    {
        for (TypeId at = type; Slot* s = SlotOf(at); at = s->info.base)
            for (const EventInfo& e : Events(at))
                if (e.name == name)
                    return &e;
        return nullptr;
    }

    const MethodInfo* MethodAt(TypeId owner, u32 index)
    {
        Slot* s = SlotOf(owner);
        if (!s || index == kNoMember)
            return nullptr;
        ModuleData* m = ModuleOf(s->info.module);
        return m && index < m->methods.Size() ? &m->methods[index] : nullptr;
    }

    const AttributeInfo* FindAttribute(TypeId owner, Range attributes, TypeId attributeType)
    {
        for (const AttributeInfo& a : Attributes(owner, attributes))
            if (IsAssignableFrom(attributeType, a.type))
                return &a;
        return nullptr;
    }
}
