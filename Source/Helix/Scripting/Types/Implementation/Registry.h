#pragma once

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Platform/Threading/Thread.h>
#include <Helix/Scripting/Types/TypeBuilder.h>

#include <atomic>

// The registry's state, shared by the registry, the builder and the reflection calls.
namespace helix::scripting::detail
{
    // A module's names and members; types hold ranges into these arrays.
    struct ModuleData
    {
        String name;
        ModuleInfo info;
        Array<char*> blocks = Array<char*>();
        char* cursor = nullptr;
        usize left = 0;
        Array<u32> types = Array<u32>();
        Array<TypeId> interfaces = Array<TypeId>();
        Array<FieldInfo> fields = Array<FieldInfo>();
        Array<MethodInfo> methods = Array<MethodInfo>();
        Array<ParameterInfo> parameters = Array<ParameterInfo>();
        Array<PropertyInfo> properties = Array<PropertyInfo>();
        Array<EventInfo> events = Array<EventInfo>();
        Array<EnumValueInfo> enumValues = Array<EnumValueInfo>();
        Array<AttributeInfo> attributes = Array<AttributeInfo>();
        Array<Variant> arguments = Array<Variant>();

        ~ModuleData();
        // A copy of the text that lives as long as the module.
        StringView Intern(StringView text);
    };

    struct Slot
    {
        TypeInfo info;
        u32 generation = 0;
        bool used = false;
        u64 hash = 0;
        // The next slot with the same name hash.
        u32 next = 0;
    };

    struct Registry
    {
        Array<ModuleData*> modules = Array<ModuleData*>();
        Array<Slot> slots = Array<Slot>();
        Array<u32> freeSlots = Array<u32>();
        HashMap<u64, u32> byName;
        // Element slot to its array type's slot.
        HashMap<u32, u32> arrays;
        ModuleId builtin;
        std::atomic<bool> ordersDirty{true};
        platform::Mutex ordersMutex;

        Registry();
        ~Registry();
    };

    Registry& R();
    // Null for invalid ids.
    Slot* SlotOf(TypeId type);
    ModuleData* ModuleOf(ModuleId module);
    TypeId MakeId(u32 slot, u32 generation);
    // The depth first numbers, brought up to date.
    void EnsureOrders();
}
