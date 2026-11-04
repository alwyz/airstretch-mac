#!/usr/bin/env bash
# Checks that the legacy Mac plugins can load on OS X 10.8 Mountain Lion.
# Run on macOS after building with -DAIRSTRETCH_LEGACY_MAC=ON:
#   scripts/check-legacy-mac.sh build-legacy/AirStretch_artefacts/Release [build.log]
#
# Checks, for each plugin binary:
#   * 64-bit Intel only (Pro Tools 11 / Mountain Lion are Intel)
#   * OS X 10.8 minimum version stamped in the binary (LC_VERSION_MIN_MACOSX),
#     and no newer-format load commands that a 10.8 loader can't read
#   * only links system libraries and frameworks that exist on 10.8
#   * with a build log compiled with -Wunguarded-availability: every API the
#     compiler flagged as newer than 10.8 is weak-linked, so the plugin still
#     loads on 10.8 (code paths using it are guarded at runtime)
set -uo pipefail

ART="${1:?usage: check-legacy-mac.sh <artefacts dir> [build log]}"
LOG="${2:-}"

newer_apis=""
if [ -n "$LOG" ] && [ -f "$LOG" ]; then
    newer_apis=$(grep -oE "'[A-Za-z_][A-Za-z0-9_]*' is only available on macOS (10\.(9|1[0-9])|1[1-9]|2[0-9])" "$LOG" \
                 | sed -E "s/^'([^']+)'.*/\1/" | sort -u)
    echo "APIs newer than 10.8 used in the build: $(echo $newer_apis | wc -w | tr -d ' ')"
fi
fail=0
bad() { echo "  FAIL: $*"; fail=1; }

# Frameworks present on OS X 10.8
ALLOWED_FRAMEWORKS="Accelerate AppKit ApplicationServices AudioToolbox AudioUnit Carbon Cocoa CoreAudio \
CoreAudioKit CoreFoundation CoreGraphics CoreMIDI CoreServices CoreText CoreVideo DiscRecording Foundation \
IOKit OpenGL QuartzCore Security SystemConfiguration WebKit AVFoundation AVKit CoreImage ImageIO"

bins=()
for b in "$ART"/VST3/*.vst3/Contents/MacOS/* "$ART"/AU/*.component/Contents/MacOS/*; do
    [ -f "$b" ] && bins+=("$b")
done
[ ${#bins[@]} -gt 0 ] || { echo "No plugin binaries found under $ART"; exit 1; }

for b in "${bins[@]}"; do
    echo "== $b"

    archs=$(lipo -archs "$b")
    echo "  architectures: $archs"
    [ "$archs" = "x86_64" ] || bad "expected x86_64 only"

    lc=$(otool -l "$b")
    if echo "$lc" | grep -q LC_VERSION_MIN_MACOSX; then
        ver=$(echo "$lc" | grep -A3 LC_VERSION_MIN_MACOSX | awk '/version/ {print $2; exit}')
        echo "  minimum OS (LC_VERSION_MIN_MACOSX): $ver"
        [ "$ver" = "10.8" ] || bad "minimum version is $ver, expected 10.8"
    else
        bad "no LC_VERSION_MIN_MACOSX load command (a 10.8 loader needs it)"
    fi
    echo "$lc" | grep -q LC_BUILD_VERSION && bad "has LC_BUILD_VERSION (macOS 10.14+ format)"
    echo "$lc" | grep -q LC_DYLD_CHAINED_FIXUPS && bad "uses chained fixups (macOS 12+ format)"
    echo "$lc" | grep -q LC_DYLD_EXPORTS_TRIE && bad "uses LC_DYLD_EXPORTS_TRIE (macOS 12+ format)"

    echo "  linked libraries:"
    while read -r lib; do
        [ -z "$lib" ] && continue
        echo "    $lib"
        case "$lib" in
            /usr/lib/libc++.1.dylib|/usr/lib/libSystem.B.dylib|/usr/lib/libobjc.A.dylib|/usr/lib/libc++abi.dylib) ;;
            /System/Library/Frameworks/*.framework/*)
                fw=$(echo "$lib" | sed -E 's|/System/Library/Frameworks/([^.]+)\.framework/.*|\1|')
                echo " $ALLOWED_FRAMEWORKS " | grep -q " $fw " || bad "framework $fw may not exist on 10.8" ;;
            *) bad "unexpected dependency $lib" ;;
        esac
    done < <(otool -L "$b" | tail -n +2 | awk '{print $1}' | grep -v "^$b")

    if [ -n "$newer_apis" ]; then
        und=$(nm -m "$b" | grep "(undefined)")
        weak=0; strong=0
        for api in $newer_apis; do
            # ObjC classes appear as _OBJC_CLASS_$_Name, functions/globals as _Name
            lines=$(echo "$und" | grep -E "[[:space:]](_OBJC_CLASS_\\\$_|_OBJC_METACLASS_\\\$_|_)${api}( |\$)" || true)
            [ -z "$lines" ] && continue        # enums/types: no symbol to link
            if echo "$lines" | grep -qv "weak"; then
                bad "$api (newer than 10.8) is strongly linked: the plugin would not load on 10.8"
                strong=$((strong + 1))
            else
                weak=$((weak + 1))
            fi
        done
        echo "  newer-than-10.8 symbols: $weak weak-linked, $strong strongly linked"
    fi
done

for p in "$ART"/VST3/*.vst3/Contents/Info.plist "$ART"/AU/*.component/Contents/Info.plist; do
    [ -f "$p" ] || continue
    min=$(/usr/libexec/PlistBuddy -c "Print :LSMinimumSystemVersion" "$p" 2>/dev/null || echo "(not set)")
    echo "== $p: LSMinimumSystemVersion $min"
done

if [ $fail -eq 0 ]; then
    echo "All legacy checks passed."
else
    echo "Some legacy checks FAILED."
fi
exit $fail
