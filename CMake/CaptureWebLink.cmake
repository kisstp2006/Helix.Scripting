# The link of a web app (cmake -P, the linker launcher helix_scripting_web_executable sets): instead of linking, the
# inputs (object files and libraries, in the link's order) and the link options are written as an MSBuild file for
# the app's project, which links them into the .NET runtime's module; the output file becomes a note.
#   -DPROPS=<file>  the MSBuild file written
# The linker command line follows the script's name and -- (a @response file is read in its place).

cmake_minimum_required(VERSION 3.28)

set(args "")
set(after_script OFF)
math(EXPR last "${CMAKE_ARGC} - 1")
foreach(i RANGE 0 ${last})
    set(a "${CMAKE_ARGV${i}}")
    if(after_script)
        list(APPEND args "${a}")
    elseif(a MATCHES "CaptureWebLink\\.cmake$")
        set(after_script ON)
    endif()
endforeach()
# The separator from cmake's own arguments, then the linker itself.
list(POP_FRONT args first)
if(first STREQUAL "--")
    list(POP_FRONT args)
endif()

# Response files: their arguments in their place (quoted ones keep their spaces).
set(expanded "")
foreach(a IN LISTS args)
    if(a MATCHES "^@(.+)$")
        file(READ "${CMAKE_MATCH_1}" text)
        string(REGEX MATCHALL "\"[^\"]*\"|[^ \t\r\n]+" items "${text}")
        foreach(item IN LISTS items)
            string(REGEX REPLACE "^\"(.*)\"$" "\\1" item "${item}")
            list(APPEND expanded "${item}")
        endforeach()
    else()
        list(APPEND expanded "${a}")
    endif()
endforeach()

set(inputs "")
set(flags "")
set(output "")
set(next "")
foreach(a IN LISTS expanded)
    if(next STREQUAL "output")
        set(output "${a}")
        set(next "")
        continue()
    elseif(next STREQUAL "flag")
        get_filename_component(path "${a}" ABSOLUTE)
        string(APPEND flags " \"${path}\"")
        set(next "")
        continue()
    endif()
    if(a STREQUAL "-o")
        set(next "output")
    elseif(a MATCHES "\\.(o|obj|a)$")
        get_filename_component(path "${a}" ABSOLUTE)
        list(APPEND inputs "${path}")
    elseif(a MATCHES "^--(js-library|pre-js|post-js)$")
        string(APPEND flags " ${a}")
        set(next "flag")
    elseif(a MATCHES "^-s(ENVIRONMENT|EXIT_RUNTIME|ALLOW_MEMORY_GROWTH)" OR a MATCHES "^-(O[0-9sz]?|g[0-9]?)$")
        # The app's build decides these.
    elseif(a MATCHES "^-")
        string(APPEND flags " ${a}")
    endif()
endforeach()

set(items "")
foreach(path IN LISTS inputs)
    string(APPEND items "    <NativeFileReference Include=\"${path}\" />\n")
endforeach()
string(REPLACE "\"" "&quot;" flags "${flags}")
file(WRITE "${PROPS}.new"
"<Project>
  <!-- Written by the link of the app (Helix.Scripting's CaptureWebLink.cmake). -->
  <ItemGroup>
${items}  </ItemGroup>
  <PropertyGroup>
    <EmccExtraLDFlags>$(EmccExtraLDFlags)${flags}</EmccExtraLDFlags>
  </PropertyGroup>
</Project>
")
file(COPY_FILE "${PROPS}.new" "${PROPS}" ONLY_IF_DIFFERENT)
file(REMOVE "${PROPS}.new")
if(output)
    file(WRITE "${output}" "// This program runs as a .NET web app: see the app's main.mjs next to its project.\n")
endif()
