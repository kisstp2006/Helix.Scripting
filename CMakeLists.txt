cmake_minimum_required(VERSION 3.28)
project(Helix.Scripting LANGUAGES CXX)

if(NOT COMMAND helix_add_library)
    include("${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.CMake/Helix.cmake")
endif()

helix_require(Core)
helix_require(Platform)
helix_add_library(helix_scripting ALIAS Scripting PUBLIC_DEPS Helix::Core Helix::Platform)

# The managed side of the .NET host (desktop): built with the .NET SDK when there is one. HELIX_SCRIPTING_RUNTIME is
# the path of Helix.Scripting.Runtime.dll (its runtimeconfig.json next to it); helix_scripting_runtime builds it.
set(scripting_managed_artifacts "${CMAKE_BINARY_DIR}/HelixScriptingManaged")
if(HELIX_PLATFORM STREQUAL "windows" OR HELIX_PLATFORM STREQUAL "linux")
    find_program(HELIX_DOTNET NAMES dotnet HINTS "$ENV{HELIX_DOTNET_ROOT}" "$ENV{DOTNET_ROOT}")
endif()
if(HELIX_DOTNET AND (HELIX_PLATFORM STREQUAL "windows" OR HELIX_PLATFORM STREQUAL "linux"))
    set(runtime_dir "${CMAKE_CURRENT_SOURCE_DIR}/Source/Managed/Helix.Scripting.Runtime")
    set(runtime_dll "${scripting_managed_artifacts}/bin/Helix.Scripting.Runtime/release/Helix.Scripting.Runtime.dll")
    file(GLOB runtime_sources CONFIGURE_DEPENDS "${runtime_dir}/*.cs" "${runtime_dir}/*.csproj")
    add_custom_command(
        OUTPUT "${runtime_dll}"
        COMMAND "${HELIX_DOTNET}" build "${runtime_dir}/Helix.Scripting.Runtime.csproj" -c Release
                --artifacts-path "${scripting_managed_artifacts}" --nologo -v quiet
        DEPENDS ${runtime_sources}
        COMMENT "Building Helix.Scripting.Runtime")
    add_custom_target(helix_scripting_runtime DEPENDS "${runtime_dll}")
    set(HELIX_SCRIPTING_RUNTIME "${runtime_dll}" CACHE INTERNAL "")
endif()

helix_add_test_suite(helix_scripting_tests DEPS Helix::Scripting)
helix_add_benchmark_suite(helix_scripting_bench DEPS Helix::Scripting)

# The host tests and benchmarks load a test assembly into the engine's load context.
if(TARGET helix_scripting_runtime AND (TARGET helix_scripting_tests OR TARGET helix_scripting_bench))
    set(tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Managed")
    set(host_tests_dll "${scripting_managed_artifacts}/bin/Helix.Scripting.HostTests/release/Helix.Scripting.HostTests.dll")
    file(GLOB_RECURSE host_tests_sources CONFIGURE_DEPENDS "${tests_dir}/*.cs" "${tests_dir}/*.csproj")
    list(FILTER host_tests_sources EXCLUDE REGEX "/(bin|obj)/")
    add_custom_command(
        OUTPUT "${host_tests_dll}"
        COMMAND "${HELIX_DOTNET}" build "${tests_dir}/HostTests/Helix.Scripting.HostTests.csproj" -c Release
                --artifacts-path "${scripting_managed_artifacts}" --nologo -v quiet
        DEPENDS ${host_tests_sources} ${runtime_sources} "${runtime_dll}"
        COMMENT "Building the C# side of the host tests")
    add_custom_target(helix_scripting_host_tests DEPENDS "${host_tests_dll}")
    foreach(target IN ITEMS helix_scripting_tests helix_scripting_bench)
        if(TARGET ${target})
            add_dependencies(${target} helix_scripting_host_tests helix_scripting_runtime)
            target_compile_definitions(${target} PRIVATE "HX_TEST_RUNTIME_ASSEMBLY=\"${runtime_dll}\"" "HX_TEST_HOST_ASSEMBLY=\"${host_tests_dll}\"")
        endif()
    endforeach()
endif()
if(TARGET helix_scripting_bench)
    target_include_directories(helix_scripting_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
endif()

# The .NET runtime is never unloaded: what it allocated is still there when the process ends (its own memory, not
# leaks of Helix), so LeakSanitizer is told about its modules.
if(TEST helix_scripting_tests AND HELIX_ENABLE_ASAN AND HELIX_PLATFORM STREQUAL "linux")
    set_tests_properties(helix_scripting_tests PROPERTIES
                         ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/Tests/Support/LeakSuppressions.txt")
endif()
