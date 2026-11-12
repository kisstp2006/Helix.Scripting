#pragma once

#include <Helix/Core/Strings/Format.h>
#include <Helix/Scripting/Types/Invoke.h>
#include <Helix/Scripting/Types/TypeBuilder.h>

#include <cstddef>

// Sample C++ types registered by hand as generated code will register them: a struct, an enum, an interface, a base
// class and a derived class with fields, properties, overloaded methods, an event and attributes.
namespace helix::scripting::testing
{
    struct Point
    {
        f32 x = 0, y = 0;
    };

    enum class Mood : i32
    {
        Calm = 0,
        Angry = 7,
    };

    struct Handler
    {
        void (*call)(void* user, i32 amount) = nullptr;
        void* user = nullptr;
    };

    class Unit
    {
    public:
        virtual ~Unit() = default;
        i32 health = 100;
        String name = String("unit");

        virtual i32 Damage(i32 amount) { return health -= amount; }
    };

    class Hero final : public Unit
    {
    public:
        Point position;
        Mood mood = Mood::Calm;
        f64 speed = 1.5;
        Array<Handler> healed = Array<Handler>();
        i32 level = 1;

        i32 Damage(i32 amount) override { return health -= amount / 2; }
        String Describe() const { return Format("{} {}", name, health); }
        String Describe(StringView prefix) const { return Format("{}{}", prefix, name); }
        f64 Scale(f64 factor) const { return speed * factor; }
        i64 Scale(i64 factor) const { return i64(level) * factor; }
        void Heal(i32 amount)
        {
            health += amount;
            for (const Handler& h : healed)
                h.call(h.user, amount);
        }
        static i32 Twice(i32 v) { return v * 2; }
    };

    struct SampleTypes
    {
        ModuleId module;
        TypeId point, mood, handler, damageable, unit, hero, range, tooltip;

        SampleTypes()
        {
            module = RegisterModule("Samples", ModuleKind::NativeGame);
            TypeId f32Type = TypeOf<f32>(), i32Type = TypeOf<i32>(), str = TypeOf<String>();
            // Attributes are types too.
            range = DeclareType(module, "Samples", "RangeAttribute", TypeKind::Class);
            tooltip = DeclareType(module, "Samples", "TooltipAttribute", TypeKind::Class);
            point = DeclareType(module, "Samples", "Point", TypeKind::Struct);
            mood = DeclareType(module, "Samples", "Mood", TypeKind::Enum);
            handler = DeclareType(module, "Samples", "Handler", TypeKind::Struct);
            damageable = DeclareType(module, "Samples", "IDamageable", TypeKind::Interface);
            unit = DeclareType(module, "Samples", "Unit", TypeKind::Class);
            hero = DeclareType(module, "Samples", "Hero", TypeKind::Class, unit, TypeSealed);

            TypeBuilder(range).Field("min", f32Type, 0u).Field("max", f32Type, 4u);
            TypeBuilder(tooltip).Field("text", str, 0u);
            TypeBuilder(point).Native<Point>().Field("x", f32Type, u32(offsetof(Point, x))).Field("y", f32Type, u32(offsetof(Point, y)));
            TypeBuilder(mood).Underlying(i32Type).EnumValue("Calm", 0).EnumValue("Angry", 7);
            BindNativeType<Mood>(mood);
            TypeBuilder(handler).Native<Handler>();
            TypeBuilder(damageable)
                .Method("Damage", i32Type, {ParameterInfo{"amount", i32Type}},
                        [](const void*, void* self, Span<const Variant> a, Variant& r, String&) {
                            r = Variant::From(static_cast<Unit*>(self)->Damage(i32(a[0].AsInt())));
                            return true;
                        },
                        nullptr, MemberPublic | MemberVirtual | MemberAbstract);
            TypeBuilder(unit)
                .Native<Unit>()
                .Interface(damageable)
                .Field("health", &Unit::health, MemberPublic | MemberSerialized)
                .Field("name", &Unit::name, MemberPublic | MemberSerialized)
                .Method("Damage", i32Type, {ParameterInfo{"amount", i32Type}},
                        [](const void*, void* self, Span<const Variant> a, Variant& r, String&) {
                            r = Variant::From(static_cast<Unit*>(self)->Damage(i32(a[0].AsInt())));
                            return true;
                        },
                        nullptr, MemberPublic | MemberVirtual);
            TypeBuilder hb(hero);
            hb.Native<Hero>()
                .Attribute(tooltip, {Variant::Text("The player")})
                .Field("position", &Hero::position, MemberPublic | MemberSerialized)
                .Field("mood", &Hero::mood, MemberPublic | MemberSerialized)
                .Field("speed", &Hero::speed, MemberPublic | MemberSerialized)
                .Attribute(range, {Variant::From(0.0f), Variant::From(10.0f)})
                // A field by accessors: the level, kept between 1 and 99.
                .Field(
                    "level", i32Type,
                    [](const void*, const void* self, Variant& out, String&) {
                        out = Variant::From(static_cast<const Hero*>(self)->level);
                        return true;
                    },
                    [](const void*, void* self, const Variant& v, String& error) {
                        i64 l = v.AsInt();
                        if (l < 1 || l > 99)
                        {
                            error = "the level is 1 to 99";
                            return false;
                        }
                        static_cast<Hero*>(self)->level = i32(l);
                        return true;
                    },
                    nullptr)
                .Method("Describe", str, {},
                        [](const void*, void* self, Span<const Variant>, Variant& r, String&) {
                            r = Variant::Text(static_cast<Hero*>(self)->Describe());
                            return true;
                        })
                .Method("Describe", str, {ParameterInfo{"prefix", str}},
                        [](const void*, void* self, Span<const Variant> a, Variant& r, String&) {
                            r = Variant::Text(static_cast<Hero*>(self)->Describe(a[0].AsText()));
                            return true;
                        })
                .Method("Scale", TypeOf<f64>(), {ParameterInfo{"factor", TypeOf<f64>()}},
                        [](const void*, void* self, Span<const Variant> a, Variant& r, String&) {
                            r = Variant::From(static_cast<Hero*>(self)->Scale(a[0].AsReal()));
                            return true;
                        })
                .Method("Scale", TypeOf<i64>(), {ParameterInfo{"factor", TypeOf<i64>()}},
                        [](const void*, void* self, Span<const Variant> a, Variant& r, String&) {
                            r = Variant::From(static_cast<Hero*>(self)->Scale(a[0].AsInt()));
                            return true;
                        })
                .Method("Twice", i32Type, {ParameterInfo{"v", i32Type}},
                        [](const void*, void*, Span<const Variant> a, Variant& r, String&) {
                            r = Variant::From(Hero::Twice(i32(a[0].AsInt())));
                            return true;
                        },
                        nullptr, MemberPublic | MemberStatic)
                .Method("Heal", TypeId(), {ParameterInfo{"amount", i32Type}},
                        [](const void*, void* self, Span<const Variant> a, Variant&, String&) {
                            static_cast<Hero*>(self)->Heal(i32(a[0].AsInt()));
                            return true;
                        })
                .Property(
                    "Health", i32Type,
                    [](const void*, void* self, Span<const Variant>, Variant& r, String&) {
                        r = Variant::From(static_cast<Hero*>(self)->health);
                        return true;
                    },
                    [](const void*, void* self, Span<const Variant> a, Variant&, String&) {
                        static_cast<Hero*>(self)->health = i32(a[0].AsInt());
                        return true;
                    })
                .Property("Strength", i32Type,
                          [](const void*, void* self, Span<const Variant>, Variant& r, String&) {
                              r = Variant::From(static_cast<Hero*>(self)->level * 10);
                              return true;
                          },
                          nullptr)
                .Event(
                    "Healed", handler,
                    [](const void*, void* self, Span<const Variant> a, Variant&, String&) {
                        Handler h;
                        a[0].Get(h);
                        static_cast<Hero*>(self)->healed.Push(h);
                        return true;
                    },
                    [](const void*, void* self, Span<const Variant> a, Variant&, String&) {
                        Handler h;
                        a[0].Get(h);
                        Array<Handler>& list = static_cast<Hero*>(self)->healed;
                        for (usize i = 0; i < list.Size(); i++)
                            if (list[i].call == h.call && list[i].user == h.user)
                            {
                                list.RemoveAt(i);
                                return true;
                            }
                        return true;
                    },
                    [](const void*, void* self, Span<const Variant> a, Variant&, String&) {
                        for (const Handler& h : static_cast<Hero*>(self)->healed)
                            h.call(h.user, i32(a[0].AsInt()));
                        return true;
                    });
            hb.Finish();
        }

        ~SampleTypes() { UnloadModule(module); }
    };
}
