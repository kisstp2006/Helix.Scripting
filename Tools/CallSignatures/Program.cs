// HelixCallSignatures --out <file.cs> <assembly.dll>...
//
// On the Web the .NET runtime is Mono's interpreter: a call through a native function pointer (calli with an
// unmanaged calling convention) needs a wrapper of its signature, and the app's build makes wrappers only for the
// signatures of the P/Invokes it sees. This tool reads the IL of the given assemblies, finds every such call and
// writes each signature once as a P/Invoke that is never called, for the app's project.

using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

string? output = null;
var inputs = new List<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--out" && i + 1 < args.Length)
        output = args[++i];
    else if (args[i].Length > 0)
        inputs.Add(args[i]);
}
if (output == null || inputs.Count == 0)
{
    Console.Error.WriteLine("usage: HelixCallSignatures --out <file.cs> <assembly.dll>...");
    return 2;
}

var readers = new List<(PEReader pe, MetadataReader md, string path)>();
foreach (string path in inputs.Distinct())
{
    if (!File.Exists(path))
    {
        Console.Error.WriteLine($"HelixCallSignatures: {path} is not there");
        return 1;
    }
    var pe = new PEReader(File.OpenRead(path));
    if (!pe.HasMetadata)
        continue;
    readers.Add((pe, pe.GetMetadataReader(), path));
}

// Enums of the assemblies by full name, with their underlying type (passed as that type).
var enums = new Dictionary<string, string>();
foreach (var (_, md, _) in readers)
    foreach (TypeDefinitionHandle h in md.TypeDefinitions)
    {
        TypeDefinition t = md.GetTypeDefinition(h);
        if (Names.FullName(md, t) is not string name || !Names.IsEnum(md, t))
            continue;
        foreach (FieldDefinitionHandle fh in t.GetFields())
        {
            FieldDefinition f = md.GetFieldDefinition(fh);
            if ((f.Attributes & FieldAttributes.Static) != 0)
                continue;
            enums[name] = f.DecodeSignature(new TypeNames(enums), null);
            break;
        }
    }

var signatures = new SortedSet<string>(StringComparer.Ordinal);
int problems = 0;
foreach (var (pe, md, path) in readers)
{
    var types = new TypeNames(enums);
    foreach (MethodDefinitionHandle mh in md.MethodDefinitions)
    {
        MethodDefinition m = md.GetMethodDefinition(mh);
        // Native code calls UnmanagedCallersOnly methods with at most 8 arguments on the Web.
        if (m.GetCustomAttributes().Any(a => Names.AttributeName(md, md.GetCustomAttribute(a)) == "System.Runtime.InteropServices.UnmanagedCallersOnlyAttribute") &&
            m.DecodeSignature(types, null).ParameterTypes.Length > 8)
        {
            Console.Error.WriteLine($"HelixCallSignatures: {Path.GetFileName(path)} {Names.Method(md, m)} is UnmanagedCallersOnly with more than 8 parameters; the Web calls such methods with 8 at most");
            problems++;
        }
        if (m.RelativeVirtualAddress == 0)
            continue;
        MethodBodyBlock body = pe.GetMethodBody(m.RelativeVirtualAddress);
        foreach (int token in IL.CalliTokens(body.GetILBytes() ?? Array.Empty<byte>()))
        {
            StandaloneSignature sig = md.GetStandaloneSignature((StandaloneSignatureHandle)MetadataTokens.EntityHandle(token));
            BlobReader blob = md.GetBlobReader(sig.Signature);
            SignatureHeader header = blob.ReadSignatureHeader();
            if (header.CallingConvention is SignatureCallingConvention.Default or SignatureCallingConvention.VarArgs)
                continue;
            blob = md.GetBlobReader(sig.Signature);
            MethodSignature<string> s = new SignatureDecoder<string, object?>(types, md, null).DecodeMethodSignature(ref blob);
            if (s.ReturnType.Contains('?') || s.ParameterTypes.Any(p => p.Contains('?')))
            {
                Console.Error.WriteLine($"HelixCallSignatures: {Path.GetFileName(path)} {Names.Method(md, m)} calls a function pointer of a generic type parameter ({string.Join(", ", s.ParameterTypes)} -> {s.ReturnType})");
                problems++;
                continue;
            }
            // Mono's WebAssembly interpreter passes at most 12 integer slots (64 bit values take two) and 12 floats.
            int ints = s.ParameterTypes.Sum(p => p is "long" or "ulong" ? 2 : p is "float" or "double" ? 0 : 1);
            int floats = s.ParameterTypes.Count(p => p is "float" or "double");
            if (ints > 12 || floats > 12)
            {
                Console.Error.WriteLine($"HelixCallSignatures: {Path.GetFileName(path)} {Names.Method(md, m)} calls a function pointer with {ints} integer slots and {floats} floats ({string.Join(", ", s.ParameterTypes)}); the Web takes 12 of each");
                problems++;
                continue;
            }
            // Structs by value: Mono's interpreter passes only those of one scalar field; the call fails when it runs.
            if (s.ParameterTypes.Any(p => p.StartsWith("global::")) || s.ReturnType.StartsWith("global::"))
                Console.WriteLine($"HelixCallSignatures: warning: {Path.GetFileName(path)} {Names.Method(md, m)} passes a struct by value to native code ({string.Join(", ", s.ParameterTypes)} -> {s.ReturnType}); on the Web only structs of one scalar field pass");
            signatures.Add(s.ReturnType + "(" + string.Join(", ", s.ParameterTypes) + ")");
        }
    }
}

var text = new StringBuilder();
text.AppendLine("// Generated by HelixCallSignatures. Do not edit.");
text.AppendLine("namespace Helix.Scripting.Browser");
text.AppendLine("{");
text.AppendLine("    /// <summary>The signatures of the app's native function pointer calls, declared for the build's wrappers; never called.</summary>");
text.AppendLine("    internal static unsafe class CallSignatures");
text.AppendLine("    {");
int n = 0;
foreach (string s in signatures)
{
    int open = s.IndexOf('(');
    string result = s[..open];
    string[] parameters = s[(open + 1)..^1].Split(", ", StringSplitOptions.RemoveEmptyEntries);
    string list = string.Join(", ", parameters.Select((p, k) => $"{p} a{k}"));
    text.AppendLine($"        [global::System.Runtime.InteropServices.DllImport(\"HelixSignatures\")] static extern {result} S{n++}({list});");
}
text.AppendLine("    }");
text.AppendLine("}");
string content = text.ToString();
Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
if (!File.Exists(output) || File.ReadAllText(output) != content)
    File.WriteAllText(output, content);
Console.WriteLine($"HelixCallSignatures: {signatures.Count} signatures of {readers.Count} assemblies");
return problems == 0 ? 0 : 1;

static class Names
{
    public static string? FullName(MetadataReader md, TypeDefinition t)
    {
        string name = md.GetString(t.Name);
        if (t.GetDeclaringType() is { IsNil: false } outer)
            return FullName(md, md.GetTypeDefinition(outer)) + "/" + name;
        string ns = md.GetString(t.Namespace);
        return ns.Length > 0 ? ns + "." + name : name;
    }

    public static string FullName(MetadataReader md, TypeReference t)
    {
        string name = md.GetString(t.Name);
        if (t.ResolutionScope.Kind == HandleKind.TypeReference)
            return FullName(md, md.GetTypeReference((TypeReferenceHandle)t.ResolutionScope)) + "/" + name;
        string ns = md.GetString(t.Namespace);
        return ns.Length > 0 ? ns + "." + name : name;
    }

    public static bool IsEnum(MetadataReader md, TypeDefinition t)
    {
        if (t.BaseType.IsNil || t.BaseType.Kind != HandleKind.TypeReference)
            return false;
        TypeReference b = md.GetTypeReference((TypeReferenceHandle)t.BaseType);
        return md.GetString(b.Namespace) == "System" && md.GetString(b.Name) == "Enum";
    }

    public static string AttributeName(MetadataReader md, CustomAttribute a)
    {
        EntityHandle type = a.Constructor.Kind switch
        {
            HandleKind.MemberReference => md.GetMemberReference((MemberReferenceHandle)a.Constructor).Parent,
            HandleKind.MethodDefinition => md.GetMethodDefinition((MethodDefinitionHandle)a.Constructor).GetDeclaringType(),
            _ => default,
        };
        return type.Kind switch
        {
            HandleKind.TypeReference => FullName(md, md.GetTypeReference((TypeReferenceHandle)type)),
            HandleKind.TypeDefinition => FullName(md, md.GetTypeDefinition((TypeDefinitionHandle)type)) ?? "",
            _ => "",
        };
    }

    public static string Method(MetadataReader md, MethodDefinition m) =>
        FullName(md, md.GetTypeDefinition(m.GetDeclaringType())) + "." + md.GetString(m.Name);
}

// C# spellings of the types of a native call: primitives as they are, pointers, references and function pointers as
// void*, enums as their underlying type, other value types by their C# name (the app's project references their
// assemblies); type parameters come back with a '?' (refused).
sealed class TypeNames : ISignatureTypeProvider<string, object?>
{
    readonly Dictionary<string, string> _enums;

    public TypeNames(Dictionary<string, string> enums) => _enums = enums;

    public string GetPrimitiveType(PrimitiveTypeCode code) => code switch
    {
        PrimitiveTypeCode.Void => "void",
        PrimitiveTypeCode.Boolean => "bool",
        PrimitiveTypeCode.Char => "char",
        PrimitiveTypeCode.SByte => "sbyte",
        PrimitiveTypeCode.Byte => "byte",
        PrimitiveTypeCode.Int16 => "short",
        PrimitiveTypeCode.UInt16 => "ushort",
        PrimitiveTypeCode.Int32 => "int",
        PrimitiveTypeCode.UInt32 => "uint",
        PrimitiveTypeCode.Int64 => "long",
        PrimitiveTypeCode.UInt64 => "ulong",
        PrimitiveTypeCode.Single => "float",
        PrimitiveTypeCode.Double => "double",
        PrimitiveTypeCode.IntPtr => "nint",
        PrimitiveTypeCode.UIntPtr => "nuint",
        _ => "void*",
    };

    public string GetPointerType(string elementType) => "void*";
    public string GetByReferenceType(string elementType) => "void*";
    public string GetFunctionPointerType(MethodSignature<string> signature) => "void*";
    public string GetPinnedType(string elementType) => elementType;
    public string GetModifiedType(string modifier, string unmodifiedType, bool isRequired) => unmodifiedType;
    public string GetSZArrayType(string elementType) => "object";
    public string GetArrayType(string elementType, ArrayShape shape) => "object";
    public string GetGenericInstantiation(string genericType, ImmutableArray<string> typeArguments)
    {
        int tick = genericType.IndexOf('`');
        return (tick < 0 ? genericType : genericType[..tick]) + "<" + string.Join(", ", typeArguments) + ">";
    }
    public string GetGenericMethodParameter(object? context, int index) => "?";
    public string GetGenericTypeParameter(object? context, int index) => "?";

    public string GetTypeFromDefinition(MetadataReader reader, TypeDefinitionHandle handle, byte rawTypeKind) =>
        Value(Names.FullName(reader, reader.GetTypeDefinition(handle)) ?? "", rawTypeKind);

    public string GetTypeFromReference(MetadataReader reader, TypeReferenceHandle handle, byte rawTypeKind) =>
        Value(Names.FullName(reader, reader.GetTypeReference(handle)), rawTypeKind);

    public string GetTypeFromSpecification(MetadataReader reader, object? context, TypeSpecificationHandle handle, byte rawTypeKind) =>
        reader.GetTypeSpecification(handle).DecodeSignature(this, context);

    string Value(string name, byte rawTypeKind)
    {
        if (rawTypeKind != (byte)SignatureTypeKind.ValueType)
            return "object";
        if (name == "System.IntPtr")
            return "nint";
        if (name == "System.UIntPtr")
            return "nuint";
        return _enums.TryGetValue(name, out string? underlying) ? underlying : "global::" + name.Replace('/', '.');
    }
}

// The calli instructions of a method body (their signature tokens).
static class IL
{
    public static IEnumerable<int> CalliTokens(byte[] il)
    {
        int i = 0;
        while (i < il.Length)
        {
            int op = il[i++];
            if (op == 0xFE)
            {
                if (i >= il.Length)
                    yield break;
                int op2 = il[i++];
                // ldarg, ldarga, starg, ldloc, ldloca, stloc (2 byte operands); unaligned (1); initobj, constrained,
                // sizeof, ldftn, ldvirtftn (tokens, 4).
                i += op2 switch { 0x09 or 0x0A or 0x0B or 0x0C or 0x0D or 0x0E => 2, 0x12 => 1, 0x06 or 0x07 or 0x15 or 0x16 or 0x1C => 4, _ => 0 };
                continue;
            }
            if (op == 0x29)
            {
                yield return BitConverter.ToInt32(il, i);
                i += 4;
                continue;
            }
            if (op == 0x45)
            {
                // switch: a count, then that many 4 byte targets.
                int count = BitConverter.ToInt32(il, i);
                i += 4 + 4 * count;
                continue;
            }
            i += Operand(op);
        }
    }

    static int Operand(int op)
    {
        if (op is >= 0x0E and <= 0x13)
            return 1; // ldarg.s ... stloc.s
        if (op is 0x1F)
            return 1; // ldc.i4.s
        if (op is 0x20)
            return 4; // ldc.i4
        if (op is 0x21)
            return 8; // ldc.i8
        if (op is 0x22)
            return 4; // ldc.r4
        if (op is 0x23)
            return 8; // ldc.r8
        if (op is 0x27 or 0x28)
            return 4; // jmp, call
        if (op is >= 0x2B and <= 0x37)
            return 1; // short branches
        if (op is >= 0x38 and <= 0x44)
            return 4; // branches
        if (op is >= 0x6F and <= 0x75)
            return 4; // callvirt, cpobj, ldobj, ldstr, newobj, castclass, isinst
        if (op is 0x79)
            return 4; // unbox
        if (op is >= 0x7B and <= 0x81)
            return 4; // ldfld ... stobj
        if (op is 0x8C or 0x8D or 0x8F)
            return 4; // box, newarr, ldelema
        if (op is >= 0xA3 and <= 0xA5)
            return 4; // ldelem, stelem, unbox.any
        if (op is 0xC2 or 0xC6)
            return 4; // refanyval, mkrefany
        if (op is 0xD0)
            return 4; // ldtoken
        if (op is 0xDD)
            return 4; // leave
        if (op is 0xDE)
            return 1; // leave.s
        return 0;
    }
}
