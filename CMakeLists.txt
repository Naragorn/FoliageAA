cmake_minimum_required(VERSION 3.20)
project(FoliageAA CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(FATAL_ERROR "Oblivion is 32-bit: configure with a 32-bit toolchain (vcvars32.bat or -A Win32).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static runtime: the plugin carries no dependency on an installed VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

add_library(coverage STATIC src/Coverage.cpp)
target_include_directories(coverage PUBLIC src)

add_library(shader_patch STATIC src/ShaderPatch.cpp)
target_include_directories(shader_patch PUBLIC src)

add_library(device_hook STATIC src/DeviceHook.cpp src/Setup.cpp)
target_include_directories(device_hook PUBLIC src)
target_link_libraries(device_hook PUBLIC coverage shader_patch)
# No d3d9.lib: the plugin reaches the device only through its method table, so
# it never pulls a second d3d9.dll into the game.

add_library(plugin_entry STATIC src/PluginEntry.cpp)
target_include_directories(plugin_entry PUBLIC src)
target_link_libraries(plugin_entry PUBLIC coverage)

add_library(FoliageAA SHARED src/Plugin.cpp)
target_link_libraries(FoliageAA PRIVATE plugin_entry device_hook)

enable_testing()

add_executable(CoverageTest tests/CoverageTest.cpp)
target_link_libraries(CoverageTest PRIVATE coverage)
add_test(NAME CoverageTest COMMAND CoverageTest)

add_executable(ShaderPatchTest tests/ShaderPatchTest.cpp)
target_link_libraries(ShaderPatchTest PRIVATE shader_patch)
add_test(NAME ShaderPatchTest COMMAND ShaderPatchTest)

add_executable(PluginEntryTest tests/PluginEntryTest.cpp)
target_link_libraries(PluginEntryTest PRIVATE plugin_entry)
add_test(NAME PluginEntryTest COMMAND PluginEntryTest)

# The two device tests create a real Direct3D 9 device on this machine.
add_executable(DeviceHookTest tests/DeviceHookTest.cpp tests/TestDevice.cpp)
target_link_libraries(DeviceHookTest PRIVATE device_hook d3d9)
add_test(NAME DeviceHookTest COMMAND DeviceHookTest)

add_executable(SetupTest tests/SetupTest.cpp tests/TestDevice.cpp)
target_link_libraries(SetupTest PRIVATE device_hook d3d9)
add_test(NAME SetupTest COMMAND SetupTest)

add_executable(ExportsTest tests/ExportsTest.cpp)
add_dependencies(ExportsTest FoliageAA)
add_test(NAME ExportsTest COMMAND ExportsTest $<TARGET_FILE:FoliageAA>)
