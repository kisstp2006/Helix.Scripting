include_guard(GLOBAL)

# Web apps of .NET (helix_add_executable ... DOTNET on the Web): the program's link is captured (CaptureWebLink.cmake)
# and a generated browser-wasm project of Helix.Scripting.Browser links its objects and libraries into the runtime's
# module (NativeFileReference) with the engine's assemblies of the target's HELIX_DOTNET_ASSEMBLIES property and the
# signatures of their native function pointer calls (Tools/CallSignatures); the app
# is published to <target>.Web/publish/wwwroot with a main.mjs for node (the target's HELIX_RUN_COMMAND).
set(HELIX_SCRIPTING_WEB_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

function(helix_scripting_web_executable target)
    set(work "${CMAKE_CURRENT_BINARY_DIR}/${target}.Web")
    set(props "${work}/Link.props")
    set(project "${work}/${target}.csproj")
    set(publish "${work}/publish")
    set(scripting "${HELIX_SCRIPTING_WEB_DIR}/..")
    file(MAKE_DIRECTORY "${work}")
    set_target_properties(${target} PROPERTIES
        CXX_LINKER_LAUNCHER "${CMAKE_COMMAND};-DPROPS=${props};-P;${HELIX_SCRIPTING_WEB_DIR}/CaptureWebLink.cmake;--")

    set(assemblies "$<TARGET_PROPERTY:${target},HELIX_DOTNET_ASSEMBLIES>")
    # Tests and other checks link at their HELIX_WEB_LINK_OPTIMIZATION (helix_add_executable CHECK), applications at
    # the SDK's release optimization.
    get_target_property(link_flag ${target} HELIX_WEB_LINK_OPTIMIZATION)
    set(link_optimization "")
    if(link_flag)
        set(link_optimization "    <EmccLinkOptimizationFlag>${link_flag}</EmccLinkOptimizationFlag>\n")
    endif()
    set(r "$<ANGLE-R>")
    set(references "$<$<BOOL:${assemblies}>:    <Reference Include=\"$<JOIN:${assemblies},\" /${r}\n    <Reference Include=\">\" /${r}\n>")
    file(GENERATE OUTPUT "${project}" CONTENT
"<Project Sdk=\"Microsoft.NET.Sdk.WebAssembly\">
  <!-- Written by helix_scripting_web_executable: the web app of ${target}. -->
  <PropertyGroup>
    <TargetFramework>net11.0</TargetFramework>
    <OutputType>Exe</OutputType>
    <AssemblyName>${target}</AssemblyName>
    <RootNamespace>Helix.Scripting.Browser</RootNamespace>
    <UseMonoRuntime>true</UseMonoRuntime>
    <WasmBuildNative>true</WasmBuildNative>
    <WasmEnableThreads>false</WasmEnableThreads>
    <PublishTrimmed>false</PublishTrimmed>
    <InvariantGlobalization>true</InvariantGlobalization>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Nullable>disable</Nullable>
    <ImplicitUsings>disable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <CompressionEnabled>false</CompressionEnabled>
    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
${link_optimization}  </PropertyGroup>
  <ItemGroup>
    <Compile Include=\"${scripting}/Source/Managed/Helix.Scripting.Browser/*.cs\" />
    <Compile Include=\"CallSignatures.g.cs\" />
    <Reference Include=\"${HELIX_SCRIPTING_RUNTIME}\" />
${references}  </ItemGroup>
  <Import Project=\"Link.props\" Condition=\"Exists('Link.props')\" />
  <PropertyGroup>
    <!-- Folders of the disk in node (main.mjs, HELIX_MOUNTS). -->
    <EmccExtraLDFlags>$(EmccExtraLDFlags) -lnodefs.js</EmccExtraLDFlags>
  </PropertyGroup>
</Project>
")
    # Written only when its text changes: the publish depends on it.
    file(CONFIGURE OUTPUT "${work}/main.mjs" @ONLY CONTENT
"// Runs the web app in node: the program's arguments, its exit code. HELIX_MOUNTS=<folder>=<path>;... mounts folders
// of the disk into the app's file system (NODEFS) before main.
import { dotnet } from './_framework/dotnet.js';
const mounts = (process.env.HELIX_MOUNTS || '').split(';').filter((m) => m.includes('=')).map((m) => m.split('='));
const mount = (module) => {
  for (const [folder, path] of mounts) {
    module.FS.mkdirTree(path);
    module.FS.mount(module.FS.filesystems.NODEFS, { root: folder }, path);
  }
};
const { runMain } = await dotnet.withModuleConfig({ preRun: [mount] }).withApplicationArguments(...process.argv.slice(2)).create();
process.exit(await runMain());
")
    set(stamp "${publish}/wwwroot/_framework/dotnet.js")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND "${HELIX_DOTNET}" "${HELIX_CALL_SIGNATURES}" --out "${work}/CallSignatures.g.cs" "${HELIX_SCRIPTING_RUNTIME}" ${assemblies}
        # The native link's tables (P/Invokes, interpreter wrappers) made again each time: the SDK's incremental build
        # kept stale ones when only the assemblies changed.
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${work}/obj/Release/net11.0/wasm"
        # Platform=AnyCPU: a Visual Studio environment sets Platform=x64, which browser-wasm refuses.
        COMMAND "${HELIX_DOTNET}" publish "${project}" -c Release -o "${publish}" -p:Platform=AnyCPU --nologo -v quiet
        COMMAND "${CMAKE_COMMAND}" -E copy "${work}/main.mjs" "${publish}/wwwroot/main.mjs"
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS "$<TARGET_FILE:${target}>" "${project}" "${work}/main.mjs" "${HELIX_SCRIPTING_RUNTIME}" "${HELIX_CALL_SIGNATURES}" ${assemblies}
                "${scripting}/Source/Managed/Helix.Scripting.Browser/Program.cs"
        COMMENT "Publishing the web app of ${target}"
        COMMAND_EXPAND_LISTS
        VERBATIM)
    add_custom_target(${target}_web ALL DEPENDS "${stamp}")
    add_dependencies(${target}_web ${target})
    foreach(tool IN ITEMS helix_scripting_runtime helix_call_signatures)
        if(TARGET ${tool})
            add_dependencies(${target}_web ${tool})
        endif()
    endforeach()
    set_target_properties(${target} PROPERTIES HELIX_RUN_COMMAND "${HELIX_EMSCRIPTEN_NODE};${publish}/wwwroot/main.mjs")
endfunction()

helix_set_dotnet_web_executable(helix_scripting_web_executable)
