#include <Support/SampleTypes.h>

#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::scripting;
using namespace helix::scripting::testing;

HX_TEST(Types, PrimitivesAreBuiltIn)
{
    for (u32 p = u32(Primitive::Void); p < u32(Primitive::Count); p++)
    {
        const TypeInfo* t = GetType(TypeOfPrimitive(Primitive(p)));
        HX_REQUIRE(t != nullptr);
        HX_CHECK(t->kind == TypeKind::Primitive && t->nameSpace == StringView("System"));
    }
    HX_CHECK(TypeOf<i32>() == FindType("System.Int32") && TypeOf<f32>() == FindType("System.Single"));
    HX_CHECK(TypeOf<String>() == TypeOf<StringView>() && GetType(TypeOf<String>())->name == StringView("String"));
    HX_CHECK((GetType(TypeOf<f64>())->flags & TypePlain) && !(GetType(TypeOf<String>())->flags & TypePlain));
}

HX_TEST(Types, RegistrationAndLookup)
{
    SampleTypes s;
    HX_REQUIRE(s.hero.IsValid() && s.unit.IsValid());
    HX_CHECK(FindType("Samples.Hero") == s.hero && TypeOf<Hero>() == s.hero && TypeOf<Point>() == s.point);
    const TypeInfo* hero = GetType(s.hero);
    HX_CHECK(hero->name == StringView("Hero") && hero->nameSpace == StringView("Samples") && hero->module == s.module);
    HX_CHECK(hero->size == sizeof(Hero) && (hero->flags & TypeSealed) && !(hero->flags & TypePlain) && hero->base == s.unit);
    HX_CHECK((GetType(s.point)->flags & TypePlain) && GetType(s.mood)->size == 4);
    // A name is taken once; a base must resolve.
    HX_CHECK(!DeclareType(s.module, "Samples", "Hero", TypeKind::Class).IsValid());
    HX_CHECK(!DeclareType(s.module, "Samples", "Orphan", TypeKind::Class, TypeId{12345}).IsValid());
    HX_CHECK(!FindType("Samples.Nobody").IsValid() && GetModule(s.module)->kind == ModuleKind::NativeGame);
    Array<TypeId> types;
    TypesOfModule(s.module, types);
    HX_CHECK(types.Size() == 8 && types[0] == s.range);
    // Members, own and inherited.
    HX_CHECK(Fields(s.hero).Size() == 4 && Fields(s.unit).Size() == 2 && Methods(s.unit).Size() == 1);
    HX_CHECK(FindField(s.hero, "health") && FindField(s.hero, "health")->owner == s.unit && !FindField(s.unit, "speed"));
    HX_CHECK(EnumValues(s.mood).Size() == 2 && EnumValues(s.mood)[1].name == StringView("Angry") && EnumValues(s.mood)[1].value == 7);
    // Properties and events made their methods.
    const PropertyInfo* health = FindProperty(s.hero, "Health");
    HX_REQUIRE(health && health->getter != kNoMember && health->setter != kNoMember);
    HX_CHECK(MethodAt(s.hero, health->getter)->name == StringView("get_Health"));
    HX_CHECK(FindProperty(s.hero, "Strength")->flags & MemberReadOnly);
    const EventInfo* healed = FindEvent(s.hero, "Healed");
    HX_REQUIRE(healed);
    HX_CHECK(MethodAt(s.hero, healed->add)->name == StringView("add_Healed") && healed->handlerType == s.handler);
}

HX_TEST(Types, InheritanceAndInterfaces)
{
    SampleTypes s;
    HX_CHECK(IsAssignableFrom(s.unit, s.hero) && !IsAssignableFrom(s.hero, s.unit));
    HX_CHECK(IsAssignableFrom(s.damageable, s.unit) && IsAssignableFrom(s.damageable, s.hero));
    HX_CHECK(IsAssignableFrom(s.hero, s.hero) && !IsAssignableFrom(s.point, s.hero) && !IsAssignableFrom(s.damageable, s.point));
    // A type declared later below an older one.
    TypeId boss = DeclareType(s.module, "Samples", "Boss", TypeKind::Class, s.unit);
    TypeBuilder(boss).Finish();
    HX_CHECK(IsAssignableFrom(s.unit, boss) && IsAssignableFrom(s.damageable, boss) && !IsAssignableFrom(s.hero, boss));
}

HX_TEST(Types, VariantConversions)
{
    SampleTypes s;
    Variant v = Variant::From(i32(-5)), out;
    HX_CHECK(v.Type() == TypeOf<i32>() && v.AsInt() == -5);
    // C#'s implicit conversions: widening and integers to reals; not narrowing, not signed to unsigned.
    HX_CHECK(v.ConvertTo(TypeOf<i64>(), out) && out.Type() == TypeOf<i64>() && out.AsInt() == -5);
    HX_CHECK(v.ConvertTo(TypeOf<f64>(), out) && out.AsReal() == -5.0);
    HX_CHECK(!v.ConvertTo(TypeOf<i16>(), out) && !v.ConvertTo(TypeOf<u32>(), out) && !v.ConvertTo(TypeOf<String>(), out));
    HX_CHECK(Variant::From(u8(200)).ConvertTo(TypeOf<i16>(), out) && out.AsInt() == 200);
    HX_CHECK(Variant::From(1.5f).ConvertTo(TypeOf<f64>(), out) && out.AsReal() == 1.5 && !Variant::From(1.5).ConvertTo(TypeOf<f32>(), out));
    HX_CHECK(v.ConversionCost(TypeOf<i32>()) == 0 && v.ConversionCost(TypeOf<i64>()) == 1 && v.ConversionCost(TypeOf<i8>()) == -1);
    // Explicit: narrowing that fits, enums, text.
    HX_CHECK(v.ConvertTo(TypeOf<i8>(), out, true) && out.AsInt() == -5 && !Variant::From(300).ConvertTo(TypeOf<i8>(), out, true));
    HX_CHECK(!v.ConvertTo(TypeOf<u32>(), out, true));
    HX_CHECK(Variant::Text("Angry").ConvertTo(s.mood, out, true) && out.AsInt() == 7 && out.Type() == s.mood);
    HX_CHECK(Variant::Text("42").ConvertTo(TypeOf<u16>(), out, true) && out.AsUInt() == 42);
    HX_CHECK(Variant::Text("0.25").ConvertTo(TypeOf<f32>(), out, true) && out.AsReal() == 0.25);
    HX_CHECK(!Variant::Text("x1").ConvertTo(TypeOf<i32>(), out, true) && Variant::Text("1").ConvertTo(TypeOf<bool>(), out, true) && out.AsBool());
    HX_CHECK(Variant::Enum(s.mood, 7).ConvertTo(TypeOf<String>(), out, true) && out.AsText() == StringView("Angry"));
    HX_CHECK(!Variant::Enum(s.mood, 7).ConvertTo(TypeOf<i32>(), out) && Variant::Enum(s.mood, 7).ConvertTo(TypeOf<i32>(), out, true));
    // Structs as bytes, inline and on the heap; Get back.
    Point p{1, 2};
    Variant pv = Variant::From(p);
    Point back;
    HX_CHECK(pv.Type() == s.point && pv.DataSize() == sizeof(Point) && pv.Get(back) && back.x == 1 && back.y == 2);
    u8 big[100];
    for (u32 i = 0; i < 100; i++)
        big[i] = u8(i);
    Variant large = Variant::Bytes(s.point, big, 100), copy = large;
    HX_CHECK(copy == large && static_cast<const u8*>(copy.Data())[99] == 99);
    Variant moved = Move(copy);
    HX_CHECK(moved == large && copy.IsVoid());
    // Objects: up to a base or interface, and null to any reference type.
    Hero h;
    Variant obj = Variant::From(&h);
    HX_CHECK(obj.Type() == s.hero && obj.ConvertTo(s.unit, out) && out.AsObject() == &h && obj.ConvertTo(s.damageable, out));
    HX_CHECK(!Variant::Object(s.unit, nullptr).ConvertTo(s.point, out) && Variant::Object(s.unit, nullptr).ConvertTo(s.hero, out));
    // Arrays of an element type.
    TypeId ints = ArrayOf(TypeOf<i32>());
    HX_CHECK(ints == ArrayOf(TypeOf<i32>()) && GetType(ints)->kind == TypeKind::Array && GetType(ints)->element == TypeOf<i32>());
    Array<Variant> items;
    items.Push(Variant::From(1));
    items.Push(Variant::From(2));
    Variant list = Variant::Items(ints, Move(items)), listCopy = list;
    HX_CHECK(list.AsItems().Size() == 2 && listCopy == list && listCopy.AsItems()[1].AsInt() == 2);
}

HX_TEST(Types, InvokeWithOverloads)
{
    SampleTypes s;
    Hero h;
    h.name = String("Ada");
    Variant result;
    String error;
    HX_CHECK(Invoke(s.hero, "Describe", &h, Span<const Variant>(), result, error) && result.AsText() == StringView("Ada 100"));
    Variant prefix = Variant::Text("Sir ");
    HX_CHECK(Invoke(s.hero, "Describe", &h, Span<const Variant>(&prefix, 1), result, error) && result.AsText() == StringView("Sir Ada"));
    // Scale(f64) and Scale(i64): an i32 converts to both; C# picks long (it converts to double, not the other way).
    Variant two = Variant::From(2);
    HX_CHECK(Invoke(s.hero, "Scale", &h, Span<const Variant>(&two, 1), result, error) && result.Type() == TypeOf<i64>() && result.AsInt() == 2);
    // Two overloads that are equally good are ambiguous: (i64, f64) and (f64, i64) for two ints.
    TypeId pair = DeclareType(s.module, "Samples", "Pair", TypeKind::Class);
    Invoker none = [](const void*, void*, Span<const Variant>, Variant&, String&) { return true; };
    TypeBuilder(pair)
        .Method("Take", TypeId(), {ParameterInfo{"a", TypeOf<i64>()}, ParameterInfo{"b", TypeOf<f64>()}}, none, nullptr, MemberPublic | MemberStatic)
        .Method("Take", TypeId(), {ParameterInfo{"a", TypeOf<f64>()}, ParameterInfo{"b", TypeOf<i64>()}}, none, nullptr, MemberPublic | MemberStatic)
        .Finish();
    const Variant ints[] = {Variant::From(1), Variant::From(2)};
    HX_CHECK(!Invoke(pair, "Take", nullptr, Span<const Variant>(ints, 2), result, error) && error.View().Contains("ambiguous"));
    Variant twoReal = Variant::From(2.0), twoLong = Variant::From(i64(3));
    HX_CHECK(Invoke(s.hero, "Scale", &h, Span<const Variant>(&twoReal, 1), result, error) && result.AsReal() == 3.0);
    HX_CHECK(Invoke(s.hero, "Scale", &h, Span<const Variant>(&twoLong, 1), result, error) && result.AsInt() == 3);
    // An overridden virtual found on the derived type; the base's method through a base pointer still dispatches.
    Variant ten = Variant::From(10);
    HX_CHECK(Invoke(s.unit, "Damage", &h, Span<const Variant>(&ten, 1), result, error) && result.AsInt() == 95);
    // Static methods need no instance; instance methods do.
    HX_CHECK(Invoke(s.hero, "Twice", nullptr, Span<const Variant>(&ten, 1), result, error) && result.AsInt() == 20);
    HX_CHECK(!Invoke(s.hero, "Describe", nullptr, Span<const Variant>(), result, error) && error.View().Contains("instance"));
    HX_CHECK(!Invoke(s.hero, "Fly", &h, Span<const Variant>(), result, error) && error.View().Contains("no method"));
    Variant text = Variant::Text("x");
    HX_CHECK(!Invoke(s.hero, "Heal", &h, Span<const Variant>(&text, 1), result, error) && error.View().Contains("no overload"));
    const TypeId argTypes[] = {TypeOf<f64>()};
    const MethodInfo* scale = FindMethod(s.hero, "Scale", Span<const TypeId>(argTypes, 1));
    HX_CHECK(scale && scale->returnType == TypeOf<f64>());
}

HX_TEST(Types, FieldsPropertiesAndEvents)
{
    SampleTypes s;
    Hero h;
    Variant v;
    String error;
    // Plain fields in place, a string field, an inherited field, a struct field, an enum field.
    HX_CHECK(GetMember(s.hero, "speed", &h, v, error) && v.AsReal() == 1.5);
    HX_CHECK(SetMember(s.hero, "speed", &h, Variant::From(3), error) && h.speed == 3.0);
    HX_CHECK(SetMember(s.hero, "name", &h, Variant::Text("Bo"), error) && h.name == StringView("Bo"));
    HX_CHECK(GetMember(s.hero, "name", &h, v, error) && v.AsText() == StringView("Bo"));
    HX_CHECK(SetMember(s.hero, "health", &h, Variant::From(i8(7)), error) && h.health == 7);
    HX_CHECK(SetMember(s.hero, "position", &h, Variant::From(Point{4, 5}), error) && h.position.y == 5);
    HX_CHECK(SetField(*FindField(s.hero, "mood"), &h, Variant::Text("Angry"), error, true) && h.mood == Mood::Angry);
    HX_CHECK(GetMember(s.hero, "mood", &h, v, error) && v.Type() == s.mood && v.AsInt() == 7);
    HX_CHECK(!SetMember(s.hero, "speed", &h, Variant::Text("fast"), error) && error.View().Contains("does not convert"));
    // A field by accessors keeps its own rules.
    HX_CHECK(SetMember(s.hero, "level", &h, Variant::From(42), error) && h.level == 42);
    HX_CHECK(!SetMember(s.hero, "level", &h, Variant::From(500), error) && error == StringView("the level is 1 to 99") && h.level == 42);
    // Properties through their methods.
    HX_CHECK(SetMember(s.hero, "Health", &h, Variant::From(55), error) && h.health == 55);
    HX_CHECK(GetMember(s.hero, "Strength", &h, v, error) && v.AsInt() == 420);
    HX_CHECK(!SetMember(s.hero, "Strength", &h, Variant::From(1), error) && error.View().Contains("read only"));
    // An event: add a handler, raise, remove.
    struct Counter
    {
        i32 total = 0;
    } counter;
    Handler handler{[](void* user, i32 amount) { static_cast<Counter*>(user)->total += amount; }, &counter};
    Variant hv = Variant::From(handler);
    HX_CHECK(AddHandler(s.hero, "Healed", &h, hv, error) && h.healed.Size() == 1);
    Variant three = Variant::From(3);
    Variant ignored;
    HX_CHECK(Invoke(s.hero, "Heal", &h, Span<const Variant>(&three, 1), ignored, error) && counter.total == 3 && h.health == 58);
    HX_CHECK(RaiseEvent(s.hero, "Healed", &h, Span<const Variant>(&three, 1), error) && counter.total == 6);
    HX_CHECK(RemoveHandler(s.hero, "Healed", &h, hv, error) && h.healed.Empty());
    HX_CHECK(!AddHandler(s.hero, "Nothing", &h, hv, error));
}

HX_TEST(Types, Attributes)
{
    SampleTypes s;
    const AttributeInfo* tip = FindAttribute(s.hero, GetType(s.hero)->attributes, s.tooltip);
    HX_REQUIRE(tip);
    HX_CHECK(AttributeArguments(s.hero, *tip)[0].AsText() == StringView("The player"));
    const FieldInfo* speed = FindField(s.hero, "speed");
    const AttributeInfo* range = FindAttribute(s.hero, speed->attributes, s.range);
    HX_REQUIRE(range);
    Span<const Variant> args = AttributeArguments(s.hero, *range);
    HX_CHECK(args.Size() == 2 && args[1].AsReal() == 10.0);
    HX_CHECK(!FindAttribute(s.hero, FindField(s.hero, "mood")->attributes, s.range) && Attributes(s.unit).Empty());
}

HX_TEST(Types, UnloadingAModule)
{
    TypeId hero, heroes;
    {
        SampleTypes s;
        hero = s.hero;
        heroes = ArrayOf(s.hero);
        HX_CHECK(GetType(heroes) != nullptr);
    }
    // Its types and their array types no longer resolve, and their names can be taken again.
    HX_CHECK(!GetType(hero) && !GetType(heroes) && !FindType("Samples.Hero").IsValid() && !FindModule("Samples").IsValid());
    SampleTypes again;
    HX_CHECK(again.hero.IsValid() && again.hero != hero && TypeOf<Hero>() == again.hero);
    HX_CHECK(Fields(again.hero).Size() == 4 && IsAssignableFrom(again.unit, again.hero));
}

HX_TEST(Types, CreatingInstances)
{
    SampleTypes s;
    void* h = CreateInstance(s.hero);
    HX_REQUIRE(h);
    Variant v;
    String error;
    HX_CHECK(GetMember(s.hero, "health", h, v, error) && v.AsInt() == 100);
    DestroyInstance(s.hero, h);
    HX_CHECK(CreateInstance(s.damageable) == nullptr);
}

namespace
{
    u32 gProvided = 0;

    void ProvideLateTypes()
    {
        gProvided++;
        ModuleId m = RegisterModule("Late", ModuleKind::NativeGame);
        TypeBuilder(DeclareType(m, "Late", "Thing", TypeKind::Class)).Finish();
    }
}

HX_TEST(Types, ProvidersRunBeforeTheFirstLookup)
{
    AddTypeProvider(&ProvideLateTypes);
    HX_CHECK(gProvided == 0);
    HX_CHECK(FindType("Late.Thing").IsValid() && gProvided == 1);
    HX_CHECK(FindType("Late.Thing").IsValid() && FindModule("Late").IsValid() && gProvided == 1);
    UnloadModule(FindModule("Late"));
}
