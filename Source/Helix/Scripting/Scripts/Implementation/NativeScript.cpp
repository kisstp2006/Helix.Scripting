#include <Helix/Scripting/Scripts/NativeScript.h>

#include <Helix/Core/Containers/Array.h>

namespace helix::scripting
{
    namespace
    {
        struct Entry
        {
            NativeScriptClass script;
            Array<NativeScriptMethod> methods;
        };

        struct Scripts
        {
            // Entries do not move (handlers keep their addresses).
            Array<Entry*> entries;
            void (*handler)(const NativeScriptClass&, bool, void*) = nullptr;
            void* user = nullptr;

            ~Scripts()
            {
                for (Entry* e : entries)
                    delete e;
            }
        };

        Scripts& S()
        {
            static Scripts s;
            return s;
        }
    }

    void RegisterNativeScriptClass(const NativeScriptClass& script)
    {
        auto* e = new Entry{script, Array<NativeScriptMethod>()};
        for (const NativeScriptMethod& m : script.methods)
            e->methods.Push(m);
        e->script.methods = Span<const NativeScriptMethod>(e->methods.Data(), e->methods.Size());
        S().entries.Push(e);
        if (S().handler)
            S().handler(e->script, true, S().user);
    }

    void UnregisterNativeScriptClasses(StringView module)
    {
        Scripts& s = S();
        for (usize i = s.entries.Size(); i-- > 0;)
        {
            Entry* e = s.entries[i];
            if (e->script.module != module)
                continue;
            if (s.handler)
                s.handler(e->script, false, s.user);
            s.entries.RemoveAt(i);
            delete e;
        }
    }

    void SetNativeScriptHandler(void (*handler)(const NativeScriptClass& script, bool added, void* user), void* user)
    {
        Scripts& s = S();
        s.handler = handler;
        s.user = user;
        if (handler)
            for (Entry* e : s.entries)
                handler(e->script, true, user);
    }

    void NativeScriptClasses(Array<const NativeScriptClass*>& out)
    {
        out.Clear();
        for (Entry* e : S().entries)
            out.Push(&e->script);
    }
}
