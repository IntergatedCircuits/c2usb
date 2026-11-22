# manifest_deps: fetch a project's third-party dependencies as declared in its west manifest,
# which stays the single source of truth. Needs only CMake and CPM (get_cpm.cmake, next to this file).
#
#   include(manifest_deps)
#   manifest_deps_add(
#       MANIFEST       <path to west.yml>
#       [SKIP_GROUPS   <group>...]  # projects in any of these west groups are not added
#       [PROVIDED      <name>...]   # projects the build environment already provides
#       [WORKSPACE_DIR <dir>]       # west topdir, default: the parent of the manifest's directory
#   )
#
# For each project in the manifest (unless skipped or provided):
#   - if <WORKSPACE_DIR>/<west path> exists and has a CMakeLists.txt, that checkout is used
#   - otherwise CPM fetches the project at its pinned revision
# A -DCPM_<name>_SOURCE=<path> given by the user always takes precedence.
#
# The manifest must follow a strict convention, so that plain CMake can parse it:
# under "projects:", each entry is "    - name: <name>" followed by one "      key: value"
# line per key (url, revision, path, clone-depth, groups: [a, b]). No remotes/defaults,
# no flow-style maps, no trailing comments on value lines. Nested blocks (e.g. import:) are
# ignored. Any other line is a configure error.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
include(get_cpm)

function(_manifest_deps_add_one workspace_dir skip_groups provided name)
    cmake_parse_arguments(PARSE_ARGV 4 dep "" "url;revision;path;clone-depth" "groups")
    foreach(group IN LISTS dep_groups)
        if(group IN_LIST skip_groups)
            return()
        endif()
    endforeach()
    if(name IN_LIST provided)
        return()
    endif()
    if(NOT dep_url OR NOT dep_revision)
        message(FATAL_ERROR "manifest: project ${name} needs both url and revision")
    endif()
    if(NOT dep_path)
        set(dep_path "${name}")
    endif()

    set(args NAME ${name} GIT_REPOSITORY ${dep_url} GIT_TAG ${dep_revision})
    if(dep_clone-depth)
        list(APPEND args GIT_SHALLOW TRUE)
    endif()
    set(local_dir "${workspace_dir}/${dep_path}")
    if(NOT DEFINED CPM_${name}_SOURCE AND EXISTS "${local_dir}/CMakeLists.txt")
        set(CPM_${name}_SOURCE "${local_dir}")
    endif()
    CPMAddPackage(${args})
endfunction()

function(manifest_deps_add)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "MANIFEST;WORKSPACE_DIR" "SKIP_GROUPS;PROVIDED")
    if(NOT arg_MANIFEST)
        message(FATAL_ERROR "manifest_deps_add: MANIFEST is required")
    endif()
    get_filename_component(manifest "${arg_MANIFEST}" ABSOLUTE)
    if(NOT arg_WORKSPACE_DIR)
        # the manifest repository is a single directory directly inside the west workspace
        get_filename_component(arg_WORKSPACE_DIR "${manifest}/../.." ABSOLUTE)
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
    file(STRINGS "${manifest}" lines)

    set(keys url revision path clone-depth groups)
    set(in_projects FALSE)
    set(name "")
    foreach(line IN LISTS lines ITEMS "<end>")
        if(line MATCHES "^ *(#.*)?$")
            continue()
        endif()
        if(NOT in_projects)
            if(line MATCHES "^  projects: *$")
                set(in_projects TRUE)
            endif()
            continue()
        endif()
        if(line MATCHES "^        " OR line MATCHES "^      import: *$")
            continue() # nested content, handled by west only
        endif()

        if(line MATCHES "^    - name: ([^ #]+) *$" OR line STREQUAL "<end>")
            if(name)
                set(dep_args "")
                foreach(k IN LISTS keys)
                    if(DEFINED val_${k})
                        list(APPEND dep_args ${k} ${val_${k}})
                    endif()
                endforeach()
                _manifest_deps_add_one("${arg_WORKSPACE_DIR}" "${arg_SKIP_GROUPS}" "${arg_PROVIDED}"
                    ${name} ${dep_args})
            endif()
            set(name "${CMAKE_MATCH_1}")
            foreach(k IN LISTS keys)
                unset(val_${k})
            endforeach()
        elseif(line MATCHES "^      groups: \\[([^]#]*)\\] *$")
            string(REGEX REPLACE " *, *" ";" val_groups "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^      (url|revision|path|clone-depth): ([^ #]+) *$")
            set(val_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
        else()
            message(FATAL_ERROR "${manifest} violates the manifest_deps convention:\n  ${line}")
        endif()
    endforeach()
endfunction()
