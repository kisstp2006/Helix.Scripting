#include <Helix/Scripting/Scripts/NativeScript.h>
#include <Helix/Testing/Test.h>

#include <new>

using namespace helix;
using namespace helix::scripting;

namespace
{
    struct Probe
    {
        i32 updates = 0;
        bool focus = false;
    };

    struct Seen
    {
        Array<String> added;
        Array<String> removed;
    };

    void Handler(const NativeScriptClass& script, bool added, void* user)
    {
        (added ? static_cast<Seen*>(user)->added : static_cast<Seen*>(user)->removed).Push(String(script.name));
    }
}

HX_TEST(NativeScripts, ClassesReachTheHandler)
{
    const NativeScriptMethod methods[] = {
        {"Update", [](void* p) { static_cast<Probe*>(p)->updates++; }, nullptr},
        {"OnApplicationFocus", nullptr, [](void* p, bool v) { static_cast<Probe*>(p)->focus = v; }},
    };
    NativeScriptClass c;
    c.module = "NativeScripts.Test";
    c.nameSpace = "Tests";
    c.name = "Probe";
    c.scriptBase = "ProbeBase";
    c.size = sizeof(Probe);
    c.alignment = alignof(Probe);
    c.construct = [](void* m) -> void* { return new (m) Probe(); };
    c.methods = Span<const NativeScriptMethod>(methods);
    c.executionOrder = -5;
    RegisterNativeScriptClass(c);

    // A handler set later sees the classes registered before it.
    Seen seen;
    SetNativeScriptHandler(&Handler, &seen);
    HX_CHECK(seen.added.Size() >= 1 && seen.added.Back() == "Probe");
    Array<const NativeScriptClass*> all;
    NativeScriptClasses(all);
    const NativeScriptClass* mine = nullptr;
    for (const NativeScriptClass* s : all)
        if (s->module == StringView("NativeScripts.Test"))
            mine = s;
    HX_REQUIRE(mine != nullptr);
    alignas(Probe) unsigned char memory[sizeof(Probe)];
    auto* p = static_cast<Probe*>(mine->construct(memory));
    mine->methods[0].call(p);
    mine->methods[1].callBool(p, true);
    HX_CHECK(p->updates == 1 && p->focus && mine->executionOrder == -5 && mine->methods.Size() == 2 && mine->scriptBase == StringView("ProbeBase"));
    UnregisterNativeScriptClasses("NativeScripts.Test");
    HX_CHECK(seen.removed.Size() == 1 && seen.removed[0] == "Probe");
    SetNativeScriptHandler(nullptr, nullptr);
}
