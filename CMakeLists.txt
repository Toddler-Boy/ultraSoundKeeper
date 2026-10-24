cmake_minimum_required (VERSION 4.2.0 FATAL_ERROR)

# No C runtime at all: own entry point, no default libraries, nothing that needs CRT support
# (security cookies, runtime checks, thread-safe statics, iterator debugging)
set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set (CMAKE_CONFIGURATION_TYPES Debug Release)

set (CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /GS-")
set (CMAKE_CXX_FLAGS_RELEASE "/O1 /Ob1 /clang:-Oz /GF /GS- /Gy /Gw /DNDEBUG")
set (CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG:NONE")

file (READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION)
string (STRIP "${VERSION}" VERSION)

project (
	ultraSoundKeeper
	VERSION ${VERSION}
	LANGUAGES CXX)

set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

set (CMAKE_CXX_STANDARD 23)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

add_executable (ultraSoundKeeper WIN32
	Source/Autostart.cpp
	Source/Autostart.h
	Source/Config.cpp
	Source/Config.h
	Source/Generator.cpp
	Source/Generator.h
	Source/GUI_TrayIcon.cpp
	Source/GUI_TrayIcon.h
	Source/Heap.h
	Source/Keeper.cpp
	Source/Keeper.h
	Source/Main.cpp
	Source/NoCrt.cpp
	Source/Power.cpp
	Source/Power.h
	Source/Stream.cpp
	Source/Stream.h
	Source/Text.h)

set_source_files_properties (Source/NoCrt.cpp PROPERTIES COMPILE_OPTIONS "/clang:-fno-builtin")

target_compile_definitions (ultraSoundKeeper PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _HAS_EXCEPTIONS=0 APP_VERSION="${VERSION}")
target_compile_options (ultraSoundKeeper PRIVATE /EHs-c- /GR- /Zc:threadSafeInit-)
target_link_options (ultraSoundKeeper PRIVATE /NODEFAULTLIB /ENTRY:entry)
target_link_libraries (ultraSoundKeeper PRIVATE kernel32 user32 gdi32 shell32 advapi32 ole32 avrt powrprof wtsapi32)
set_property (DIRECTORY PROPERTY VS_STARTUP_PROJECT ultraSoundKeeper)
