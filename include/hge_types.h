#ifndef HGE_TYPES_H
#define HGE_TYPES_H

// Cross-platform replacement for the Win32 type soup that HGE 1.81 relied on.
//
// IMPORTANT: in the original HGE these were Win32 typedefs. On Linux/x86-64
// "DWORD" from <windows.h> is 32-bit, but the naive substitute "unsigned long"
// would be 64-bit and silently corrupt HGE's vertex layout and ARGB packing.
// Everything below is fixed width on purpose.

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef int8_t   i8;
typedef uint16_t u16;
typedef int16_t  i16;
typedef uint32_t u32;
typedef int32_t  i32;
typedef uint64_t u64;
typedef int64_t  i64;
typedef float    f32;
typedef double   f64;

typedef uint32_t DWORD;   // fixed 32-bit, mirrors Win32 DWORD semantics
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int32_t  BOOL;
typedef void*    LPVOID;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Win32 __stdcall is meaningless elsewhere; keep the call sites identical.
#ifndef HGE_CALL
#define HGE_CALL
#endif

// Opaque handles that were Win32 types in the original public interface.
typedef void* HGE_HWND;
typedef void* HGE_HICON;
typedef void* HGE_HINSTANCE;

// ARGB helpers. HGE stores colours as 0xAARRGGBB in a DWORD, which in memory
// (little endian) is B,G,R,A -- that is why the GL backend swizzles .bgra.
#define HGE_ARGB(a, r, g, b) \
    ((DWORD)(((u32)(a) << 24) | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b)))
#define HGE_GETA(c) ((u8)(((c) >> 24) & 0xFF))
#define HGE_GETR(c) ((u8)(((c) >> 16) & 0xFF))
#define HGE_GETG(c) ((u8)(((c) >> 8) & 0xFF))
#define HGE_GETB(c) ((u8)((c) & 0xFF))

#endif // HGE_TYPES_H
