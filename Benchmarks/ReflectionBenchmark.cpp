#include <Helix/Scripting/Types/Invoke.h>
#include <Helix/Scripting/Types/TypeBuilder.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Testing/Bench.h>

#include <cstddef>

using namespace helix;
using namespace helix::scripting;

namespace
{
    struct Body
    {
        f32 mass = 1;
        i32 hits = 0;
    };

    struct Types
    {
        ModuleId module;
        TypeId body;

        Types()
        {
            module = RegisterModule("Bench", ModuleKind::NativeGame);
            body = DeclareType(module, "Bench", "Body", TypeKind::Class);
            // Padding types so lookups by name search a registry of realistic size.
            for (u32 i = 0; i < 2000; i++)
                TypeBuilder(DeclareType(module, "Bench.Padding", Format("Type{}", i), TypeKind::Class)).Finish();
            TypeBuilder(body)
                .Native<Body>()
                .Field("mass", TypeOf<f32>(), u32(offsetof(Body, mass)))
                .Method("Hit", TypeOf<i32>(), {ParameterInfo{"amount", TypeOf<i32>()}},
                        [](const void*, void* self, Span<const Variant> a, Variant& r, String&) {
                            Body& b = *static_cast<Body*>(self);
                            b.hits += i32(a[0].AsInt());
                            r = Variant::From(b.hits);
                            return true;
                        });
        }
        ~Types() { UnloadModule(module); }
    };
}

HX_BENCH(ScriptingInvokeResolved)
{
    Types t;
    Body b;
    const MethodInfo* hit = FindMethod(t.body, "Hit", Span<const TypeId>());
    const TypeId args[] = {TypeOf<i32>()};
    hit = FindMethod(t.body, "Hit", Span<const TypeId>(args, 1));
    Variant one = Variant::From(1), result;
    String error;
    while (state.KeepRunning())
        Invoke(*hit, &b, Span<const Variant>(&one, 1), result, error);
    helix::testing::DoNotOptimize(b.hits);
}

HX_BENCH(ScriptingInvokeByName)
{
    Types t;
    Body b;
    Variant one = Variant::From(1), result;
    String error;
    while (state.KeepRunning())
        Invoke(t.body, "Hit", &b, Span<const Variant>(&one, 1), result, error);
    helix::testing::DoNotOptimize(b.hits);
}

HX_BENCH(ScriptingFieldReadResolved)
{
    Types t;
    Body b;
    const FieldInfo* mass = FindField(t.body, "mass");
    Variant v;
    String error;
    f64 sum = 0;
    while (state.KeepRunning())
    {
        GetField(*mass, &b, v, error);
        sum += v.AsReal();
    }
    helix::testing::DoNotOptimize(sum);
}

HX_BENCH(ScriptingFindTypeByName)
{
    Types t;
    u32 found = 0;
    while (state.KeepRunning())
        found += FindType("Bench.Padding.Type1234").IsValid() ? 1 : 0;
    helix::testing::DoNotOptimize(found);
}
