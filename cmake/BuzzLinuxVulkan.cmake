# Integrate the SDL host hooks without changing the pinned SDK checkout.
set(sdk_vulkan "${PS3RECOMP_DIR}/libs/video/rsx_vulkan_backend.c")
set(vulkan_patch "${CMAKE_CURRENT_LIST_DIR}/BuzzLinuxVulkan.patch")
set(patched_vulkan "${CMAKE_CURRENT_BINARY_DIR}/rsx_vulkan_backend.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sdk_vulkan}" "${vulkan_patch}")
find_program(PATCH_EXECUTABLE patch REQUIRED)
# A shared Windows checkout can contain CRLF even when building in WSL.
file(READ "${sdk_vulkan}" sdk_text)
string(REPLACE "\r\n" "\n" sdk_text "${sdk_text}")
file(WRITE "${patched_vulkan}.source" "${sdk_text}")
file(REMOVE "${patched_vulkan}.tmp")
execute_process(COMMAND "${PATCH_EXECUTABLE}" --batch --forward --fuzz=0
    -o "${patched_vulkan}.tmp" "${patched_vulkan}.source" "${vulkan_patch}"
    COMMAND_ERROR_IS_FATAL ANY)
configure_file("${patched_vulkan}.tmp" "${patched_vulkan}" COPYONLY)
file(REMOVE "${patched_vulkan}.tmp" "${patched_vulkan}.source")
get_target_property(sdk_sources ps3recomp_runtime SOURCES)
list(REMOVE_ITEM sdk_sources "${sdk_vulkan}")
set_property(TARGET ps3recomp_runtime PROPERTY SOURCES "${sdk_sources}")
target_sources(ps3recomp_runtime PRIVATE "${patched_vulkan}")
set_source_files_properties("${patched_vulkan}" TARGET_DIRECTORY ps3recomp_runtime PROPERTIES
    INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/libs/video")
