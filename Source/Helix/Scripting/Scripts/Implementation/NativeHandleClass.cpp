#include <Helix/Scripting/Scripts/NativeHandleClass.h>

#include <Helix/Core/Containers/Array.h>

namespace helix::scripting
{
    namespace
    {
        struct Entry
        {
            NativeHandleClass c;
            Array<NativeTag> tags;
            Array<NativeHandleField> fields;
            Array<Array<NativeTag>> fieldTags;
        };

        struct Maker
        {
            String base;
            void (*handler)(const NativeHandleClass&, bool, void*) = nullptr;
            void* user = nullptr;
            void* (*lookup)(u64, const void*, void*) = nullptr;
            void* lookupUser = nullptr;
        };

        struct Classes
        {
            // Entries do not move (handlers keep their addresses).
            Array<Entry*> entries;
            Array<Maker> makers;

            ~Classes()
            {
                for (Entry* e : entries)
                    delete e;
            }

            Maker& MakerOf(StringView base)
            {
                for (Maker& m : makers)
                    if (m.base.View() == base)
                        return m;
                Maker& m = makers.Emplace();
                m.base = String(base);
                return m;
            }
        };

        Classes& C()
        {
            static Classes c;
            return c;
        }
    }

    const NativeTag* NativeHandleClass::Tag(StringView k) const
    {
        for (const NativeTag& t : tags)
            if (t.key == k)
                return &t;
        return nullptr;
    }

    void RegisterNativeHandleClass(const NativeHandleClass& c)
    {
        auto* e = new Entry{c, Array<NativeTag>(), Array<NativeHandleField>(), Array<Array<NativeTag>>()};
        for (const NativeTag& t : c.tags)
            e->tags.Push(t);
        e->c.tags = Span<const NativeTag>(e->tags.Data(), e->tags.Size());
        e->fieldTags.Resize(c.fields.Size());
        for (usize i = 0; i < c.fields.Size(); i++)
        {
            for (const NativeTag& t : c.fields[i].tags)
                e->fieldTags[i].Push(t);
            NativeHandleField f = c.fields[i];
            f.tags = Span<const NativeTag>(e->fieldTags[i].Data(), e->fieldTags[i].Size());
            e->fields.Push(f);
        }
        e->c.fields = Span<const NativeHandleField>(e->fields.Data(), e->fields.Size());
        C().entries.Push(e);
        Maker& m = C().MakerOf(c.base);
        if (m.handler)
            m.handler(e->c, true, m.user);
    }

    void UnregisterNativeHandleClasses(StringView module)
    {
        Classes& s = C();
        for (usize i = s.entries.Size(); i-- > 0;)
        {
            Entry* e = s.entries[i];
            if (e->c.module != module)
                continue;
            Maker& m = s.MakerOf(e->c.base);
            if (m.handler)
                m.handler(e->c, false, m.user);
            s.entries.RemoveAt(i);
            delete e;
        }
    }

    void SetNativeHandleHandler(StringView base, void (*handler)(const NativeHandleClass& c, bool added, void* user), void* user)
    {
        Maker& m = C().MakerOf(base);
        m.handler = handler;
        m.user = user;
        if (handler)
            for (Entry* e : C().entries)
                if (e->c.base == base)
                    handler(e->c, true, user);
    }

    void SetNativeHandleLookup(StringView base, void* (*lookup)(u64 id, const void* key, void* user), void* user)
    {
        Maker& m = C().MakerOf(base);
        m.lookup = lookup;
        m.lookupUser = user;
    }

    void* FindNativeHandleObject(StringView base, u64 id, const void* key)
    {
        Maker& m = C().MakerOf(base);
        return m.lookup && id != 0 ? m.lookup(id, key, m.lookupUser) : nullptr;
    }
}
