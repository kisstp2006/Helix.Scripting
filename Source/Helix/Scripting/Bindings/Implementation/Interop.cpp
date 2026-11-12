#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Types/TypeBuilder.h>

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Logging/Log.h>

#include <cstring>

namespace helix::scripting
{
    namespace
    {
        thread_local Array<char> gStringBuffer;
        thread_local Array<u8> gArrayBuffer;

        struct Table
        {
            String module;
            const void* const* native = nullptr;
            u32 nativeCount = 0;
            void* const* managed = nullptr;
            u32 managedCount = 0;
        };

        Array<Table>& Tables()
        {
            static Array<Table> t;
            return t;
        }

        Table& TableOf(StringView module)
        {
            for (Table& t : Tables())
                if (t.module == module)
                    return t;
            Table& t = Tables().Emplace();
            t.module = String(module);
            return t;
        }
    }

    namespace
    {
        LogChannel gBindingsLog{"Bindings"};
    }

    void ReportBindingFailure(StringView what, i32 status)
    {
        const char* why = status == InteropMissingObject      ? "the object is missing (destroyed or null)"
                          : status == InteropNullArgument     ? "a null argument where an object is needed"
                          : status == InteropManagedException ? "the C# member threw"
                          : status == InteropNotLoaded        ? "the other side's function table is not loaded"
                                                              : "the call failed";
        HX_LOG_ERROR(gBindingsLog, "{}: {} (status {})", what, why, status);
    }

    TypeId EventHandlerType()
    {
        static TypeId type = [] {
            ModuleId module = FindModule("Helix.Scripting");
            if (!module.IsValid())
                module = RegisterModule("Helix.Scripting", ModuleKind::Engine);
            TypeId t = DeclareType(module, "Helix.Scripting", "EventHandler", TypeKind::Struct);
            TypeBuilder b(t);
            b.Native<EventHandler>();
            return b.Finish();
        }();
        return type;
    }

    InteropString PassString(StringView text)
    {
        gStringBuffer.Resize(text.Size() + 1);
        if (!text.Empty())
            std::memcpy(gStringBuffer.Data(), text.Data(), text.Size());
        gStringBuffer[text.Size()] = 0;
        return InteropString{gStringBuffer.Data(), i32(text.Size())};
    }

    InteropArray PassArray(const void* items, usize itemSize, usize count)
    {
        gArrayBuffer.Resize(itemSize * count);
        if (count)
            std::memcpy(gArrayBuffer.Data(), items, itemSize * count);
        return InteropArray{gArrayBuffer.Data(), i32(count)};
    }

    String StringFromUtf16(const char16_t* data, i32 size)
    {
        String out;
        for (i32 i = 0; i < size; i++)
        {
            u32 c = data[i];
            // Surrogate pairs.
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < size && data[i + 1] >= 0xDC00 && data[i + 1] <= 0xDFFF)
                c = 0x10000 + ((c - 0xD800) << 10) + (u32(data[++i]) - 0xDC00);
            if (c < 0x80)
                out.Append(char(c));
            else if (c < 0x800)
            {
                out.Append(char(0xC0 | (c >> 6)));
                out.Append(char(0x80 | (c & 0x3F)));
            }
            else if (c < 0x10000)
            {
                out.Append(char(0xE0 | (c >> 12)));
                out.Append(char(0x80 | ((c >> 6) & 0x3F)));
                out.Append(char(0x80 | (c & 0x3F)));
            }
            else
            {
                out.Append(char(0xF0 | (c >> 18)));
                out.Append(char(0x80 | ((c >> 12) & 0x3F)));
                out.Append(char(0x80 | ((c >> 6) & 0x3F)));
                out.Append(char(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    void RegisterNativeFunctions(StringView module, const void* const* table, u32 count)
    {
        Table& t = TableOf(module);
        t.native = table, t.nativeCount = count;
    }

    Span<const void* const> NativeFunctions(StringView module)
    {
        Table& t = TableOf(module);
        return Span<const void* const>(t.native, t.nativeCount);
    }

    void RegisterManagedFunctions(StringView module, void* const* table, u32 count)
    {
        Table& t = TableOf(module);
        t.managed = table, t.managedCount = count;
    }

    Span<void* const> ManagedFunctions(StringView module)
    {
        Table& t = TableOf(module);
        return Span<void* const>(t.managed, t.managedCount);
    }
}
