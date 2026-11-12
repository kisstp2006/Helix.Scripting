#include <Support/SampleTypes.h>

#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Bindings/Marshal.h>
#include <Helix/Testing/Test.h>

#include <cstring>

using namespace helix;
using namespace helix::scripting;
using namespace helix::scripting::testing;

HX_TEST(Interop, StringsAndArraysCross)
{
    // "a", U+00E9, U+4E16, U+1F600 (a surrogate pair) from UTF-16 to UTF-8.
    const char16_t text[] = {u'a', 0x00E9, 0x4E16, 0xD83D, 0xDE00};
    String s = StringFromUtf16(text, 5);
    HX_CHECK(s.Size() == 1 + 2 + 3 + 4 && std::memcmp(s.Data(), "a\xC3\xA9\xE4\xB8\x96\xF0\x9F\x98\x80", 10) == 0);
    InteropString out = PassString(s);
    HX_CHECK(out.size == 10 && out.data[10] == 0 && std::memcmp(out.data, s.Data(), 10) == 0);
    HX_CHECK(PassString(StringView()).size == 0);

    i32 values[] = {3, 1, 4};
    InteropArray a = PassArray(values, sizeof(i32), 3);
    HX_CHECK(a.count == 3 && static_cast<const i32*>(a.data)[2] == 4 && a.data != values);
}

HX_TEST(Interop, FunctionTablesByModule)
{
    static const void* const native[] = {&native, nullptr};
    RegisterNativeFunctions("Interop.Test", native, 1);
    HX_CHECK(NativeFunctions("Interop.Test").Size() == 1 && NativeFunctions("Interop.Test")[0] == &native);
    HX_CHECK(NativeFunctions("Interop.None").Size() == 0);
    static void* managed[] = {nullptr, nullptr};
    RegisterManagedFunctions("Interop.Test", managed, 2);
    HX_CHECK(ManagedFunctions("Interop.Test").Size() == 2 && NativeFunctions("Interop.Test").Size() == 1);
}

HX_TEST(Interop, EventHandlersAndObjectsAsVariants)
{
    TypeId handler = EventHandlerType();
    HX_REQUIRE(handler.IsValid());
    HX_CHECK(handler == EventHandlerType() && FindType("Helix.Scripting.EventHandler") == handler);
    int user = 0;
    Variant v = Variant::From(EventHandler{&user, &user});
    EventHandler back;
    HX_CHECK(v.Type() == handler && v.Get(back) && back.user == &user);

    // Object pointers convert to the pointee's type and its bases (and from null).
    SampleTypes s;
    Hero hero;
    Variant obj = Variant::Object(s.hero, &hero);
    Unit* unit = nullptr;
    Hero* again = nullptr;
    HX_CHECK(obj.Get(unit) && unit == &hero && obj.Get(again) && again == &hero);
    Variant none = Variant::Object(s.unit, nullptr);
    HX_CHECK(none.Get(unit) && unit == nullptr);

    // Arrays of objects and of values through the marshalling helpers.
    Array<Unit*> units;
    units.Push(&hero);
    units.Push(nullptr);
    Variant items = ToVariant(units);
    HX_CHECK(GetType(items.Type())->element == s.unit && items.AsItems().Size() == 2);
    Array<Unit*> read;
    HX_CHECK(FromVariant(items, read) && read.Size() == 2 && read[0] == &hero && read[1] == nullptr);
    Array<i32> numbers;
    numbers.Push(5);
    Array<i32> numbersBack;
    HX_CHECK(FromVariant(ToVariant(numbers), numbersBack) && numbersBack.Size() == 1 && numbersBack[0] == 5);
}
