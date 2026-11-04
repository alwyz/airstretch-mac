# Patches JUCE 6.1.6 so it builds with current Xcode (used by the legacy
# OS X 10.8 build). Run by FetchContent as a PATCH_COMMAND; safe to re-run.
#
# CGWindowListCreateImage is obsoleted in the macOS 15 SDK. JUCE builds its
# juceaide helper natively for the build machine's macOS, where the call is a
# hard error. Skip it when compiling for macOS 14+; the 10.8 plugin keeps it.

if(NOT JUCE_SRC)
    message(FATAL_ERROR "PatchJuce6.cmake: pass -DJUCE_SRC=<JUCE source dir>")
endif()

set(file "${JUCE_SRC}/modules/juce_gui_basics/native/juce_mac_Windowing.mm")
file(READ "${file}" text)

string(FIND "${text}" "AIRSTRETCH_PATCH" already)
if(already GREATER -1)
    message(STATUS "JUCE 6 patch already applied")
    return()
endif()

set(old "        CGImageRef screenShot = CGWindowListCreateImage (CGRectNull,
                                                         kCGWindowListOptionIncludingWindow,
                                                         (CGWindowID) [nsWindow windowNumber],
                                                         kCGWindowImageBoundsIgnoreFraming);
")
set(new "       #if defined (MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 140000
        // AIRSTRETCH_PATCH: API obsoleted in the macOS 15 SDK; only reached
        // when building tools for a modern build machine.
        ignoreUnused (nsWindow);
        CGImageRef screenShot = nullptr;
        if (screenShot == nullptr)
            return {};
       #else
        CGImageRef screenShot = CGWindowListCreateImage (CGRectNull,
                                                         kCGWindowListOptionIncludingWindow,
                                                         (CGWindowID) [nsWindow windowNumber],
                                                         kCGWindowImageBoundsIgnoreFraming);
       #endif
")

string(FIND "${text}" "${old}" pos)
if(pos EQUAL -1)
    message(FATAL_ERROR "PatchJuce6.cmake: expected code not found in ${file}")
endif()
string(REPLACE "${old}" "${new}" text "${text}")
file(WRITE "${file}" "${text}")
message(STATUS "Patched JUCE 6 for the current macOS SDK")
