// Only compiled for MinGW builds: JUCE's speech-synthesis code expects this
// GUID from a Microsoft SDK library that MinGW does not provide.
#include <windows.h>
extern "C" const CLSID CLSID_SpVoice = { 0x96749377, 0x3391, 0x11D2, { 0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96 } };
