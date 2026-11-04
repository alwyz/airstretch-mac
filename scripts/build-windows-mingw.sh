#!/usr/bin/env bash
# Cross-compile the Windows VST3 from Linux with llvm-mingw (unofficial:
# JUCE 8 no longer supports MinGW, but this build works). For release
# builds, prefer MSVC via the GitHub Actions workflow.
#
# Usage: scripts/build-windows-mingw.sh [work-dir]   (default: ./build-mingw-work)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mkdir -p "${1:-$ROOT/build-mingw-work}" && cd "${1:-$ROOT/build-mingw-work}" && pwd)"
LLVM_MINGW=llvm-mingw-20241119-ucrt-ubuntu-20.04-x86_64
cd "$WORK"

# 1. Toolchain and JUCE
if [ ! -d "$LLVM_MINGW" ]; then
    curl -L -o llvm-mingw.tar.xz "https://github.com/mstorsjo/llvm-mingw/releases/download/20241119/$LLVM_MINGW.tar.xz"
    tar -xJf llvm-mingw.tar.xz
fi
TC="$WORK/$LLVM_MINGW/bin"
if [ ! -d JUCE ]; then
    git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git
fi

# 2. JUCE patches for MinGW (idempotent)
python3 - JUCE/modules <<'EOF'
import sys, pathlib
m = pathlib.Path(sys.argv[1])
p = m / "juce_gui_basics/native/accessibility/juce_UIATextProvider_windows.h"
s = p.read_text()
if "CaretPosition_Unknown = 0" not in s:
    s = s.replace("namespace juce", "#if defined(__MINGW32__)\nenum { CaretPosition_Unknown = 0, CaretPosition_EndOfLine = 1, CaretPosition_BeginningOfLine = 2 };\n#endif\n\nnamespace juce", 1)
    p.write_text(s)
p = m / "juce_graphics/native/juce_Direct2DResources_windows.cpp"
s = p.read_text().replace("__uuidof (device)", "__uuidof (IDXGIDevice)").replace("__uuidof (surface)", "__uuidof (IDXGISurface)")
p.write_text(s)
EOF

# 3. Stub X11 extension headers so juceaide (built natively) compiles
mkdir -p xinc/X11/extensions xinc/X11/Xcursor
cat > xinc/X11/extensions/Xrandr.h <<'EOF'
#pragma once
#include <X11/Xlib.h>
#include <X11/extensions/randr.h>
typedef XID RROutput; typedef XID RRCrtc; typedef XID RRMode;
typedef unsigned long XRRModeFlags;
typedef struct { RRMode id; unsigned int width, height; unsigned long dotClock; unsigned int hSyncStart, hSyncEnd, hTotal, hSkew, vSyncStart, vSyncEnd, vTotal; char* name; unsigned int nameLength; XRRModeFlags modeFlags; } XRRModeInfo;
typedef struct { Time timestamp, configTimestamp; int ncrtc; RRCrtc* crtcs; int noutput; RROutput* outputs; int nmode; XRRModeInfo* modes; } XRRScreenResources;
typedef struct { Time timestamp; RRCrtc crtc; char* name; int nameLen; unsigned long mm_width, mm_height; Connection connection; SubpixelOrder subpixel_order; int ncrtc; RRCrtc* crtcs; int nclone; RROutput* clones; int nmode, npreferred; RRMode* modes; } XRROutputInfo;
typedef struct { Time timestamp; int x, y; unsigned int width, height; RRMode mode; Rotation rotation; int noutput; RROutput* outputs; Rotation rotations; int npossible; RROutput* possible; } XRRCrtcInfo;
XRRScreenResources* XRRGetScreenResources (Display*, Window);
void XRRFreeScreenResources (XRRScreenResources*);
XRROutputInfo* XRRGetOutputInfo (Display*, XRRScreenResources*, RROutput);
void XRRFreeOutputInfo (XRROutputInfo*);
XRRCrtcInfo* XRRGetCrtcInfo (Display*, XRRScreenResources*, RRCrtc);
void XRRFreeCrtcInfo (XRRCrtcInfo*);
RROutput XRRGetOutputPrimary (Display*, Window);
EOF
cat > xinc/X11/extensions/Xinerama.h <<'EOF'
#pragma once
#include <X11/Xlib.h>
typedef struct { int screen_number; short x_org, y_org, width, height; } XineramaScreenInfo;
Bool XineramaIsActive (Display*);
XineramaScreenInfo* XineramaQueryScreens (Display*, int*);
EOF
cat > xinc/X11/extensions/Xcomposite.h <<'EOF'
#pragma once
#include <X11/Xlib.h>
EOF
cat > xinc/X11/Xcursor/Xcursor.h <<'EOF'
#pragma once
#include <X11/Xlib.h>
typedef unsigned int XcursorUInt; typedef XcursorUInt XcursorDim; typedef XcursorUInt XcursorPixel;
typedef struct { XcursorUInt version; XcursorDim size, width, height, xhot, yhot; XcursorUInt delay; XcursorPixel* pixels; } XcursorImage;
XcursorImage* XcursorImageCreate (int, int);
void XcursorImageDestroy (XcursorImage*);
Cursor XcursorImageLoadCursor (Display*, const XcursorImage*);
EOF
export CPLUS_INCLUDE_PATH="$WORK/xinc" C_INCLUDE_PATH="$WORK/xinc"

# 4. Header case fix: JUCE includes <Dbghelp.h>, MinGW ships dbghelp.h
mkdir -p wincase
ln -sf "$TC/../x86_64-w64-mingw32/include/dbghelp.h" wincase/Dbghelp.h

# 5. Toolchain file
cat > mingw.cmake <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $TC/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER $TC/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER $TC/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH $TC/../x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_CXX_FLAGS_INIT "-I$WORK/wincase")
set(CMAKE_C_FLAGS_INIT "-I$WORK/wincase")
EOF

# 6. Build
cmake -S "$ROOT" -B "$WORK/build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$WORK/mingw.cmake" \
      -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$WORK/JUCE" -DAIRSTRETCH_BUILD_TESTS=OFF
cmake --build "$WORK/build"

# 7. Verify and strip
BIN="$WORK/build/AirStretch_artefacts/Release/VST3/AirStretch.vst3/Contents/x86_64-win/AirStretch.vst3"
"$TC/llvm-objdump" -p "$BIN" | grep -E "GetPluginFactory|InitDll|ExitDll"
"$TC/llvm-strip" "$BIN"
echo "Built: $WORK/build/AirStretch_artefacts/Release/VST3/AirStretch.vst3"
