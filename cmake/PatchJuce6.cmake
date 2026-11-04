# Patches JUCE 6.1.6 so it builds with current Xcode (used by the legacy
# OS X 10.8 build). Run by FetchContent as a PATCH_COMMAND; safe to re-run.
#
# 1. CGWindowListCreateImage is obsoleted in the macOS 15 SDK. JUCE builds its
#    juceaide helper natively for the build machine's macOS, where the call is
#    a hard error. Skip it when compiling for macOS 14+; the 10.8 plugin keeps it.
#
# 2. The AU's legacy Component Manager resource (.rsrc, built with Rez from
#    AudioUnit.r) is only used by OS X 10.6 and earlier hosts; 10.7+ register
#    AUs from Info.plist. Current SDKs no longer ship AudioUnit.r, so skip the
#    step, as JUCE 7 does.

if(NOT JUCE_SRC)
    message(FATAL_ERROR "PatchJuce6.cmake: pass -DJUCE_SRC=<JUCE source dir>")
endif()

function(airstretch_patch file old new)
    file(READ "${file}" text)
    string(FIND "${text}" "AIRSTRETCH_PATCH" already)
    if(already GREATER -1)
        message(STATUS "Already patched: ${file}")
        return()
    endif()
    string(FIND "${text}" "${old}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "PatchJuce6.cmake: expected code not found in ${file}")
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    file(WRITE "${file}" "${text}")
    message(STATUS "Patched: ${file}")
endfunction()

# 1. Window snapshot API
airstretch_patch ("${JUCE_SRC}/modules/juce_gui_basics/native/juce_mac_Windowing.mm"
"        CGImageRef screenShot = CGWindowListCreateImage (CGRectNull,
                                                         kCGWindowListOptionIncludingWindow,
                                                         (CGWindowID) [nsWindow windowNumber],
                                                         kCGWindowImageBoundsIgnoreFraming);
"
"       #if defined (MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED >= 140000
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

# 2. AU resource fork
airstretch_patch ("${JUCE_SRC}/extras/Build/CMake/JUCEUtils.cmake"
"function(_juce_add_au_resource_fork shared_code_target au_target)
    if(NOT JUCE_XCRUN)"
"function(_juce_add_au_resource_fork shared_code_target au_target)
    # AIRSTRETCH_PATCH: legacy .rsrc is only for OS X 10.6 and earlier hosts,
    # and current SDKs no longer ship AudioUnit.r.
    return()
    if(NOT JUCE_XCRUN)")
