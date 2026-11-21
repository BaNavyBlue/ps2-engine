/* (build 125) The game's pack (tools/make_pack.py, build/pack.bin) linked
 * into the program as read-only data: ps2_pack to ps2_pack_end, read by
 * src/core/assetfs.c when built with PS2_PACKED (`make dist`). Assembled
 * from the tree's top, where the path below is.
 * (macOS) Mach-O names read-only data __TEXT,__const and gives C's names a
 * leading underscore; ELF and COFF take them as written. */

#if defined(__APPLE__)
#define PACK     _ps2_pack
#define PACK_END _ps2_pack_end
    .section __TEXT,__const
#else
#define PACK     ps2_pack
#define PACK_END ps2_pack_end
    .section .rodata
#endif
    .globl  PACK
    .globl  PACK_END
    .p2align 4
PACK:
    .incbin "build/pack.bin"
PACK_END:
    .byte   0

#if defined(__ELF__)
    .section .note.GNU-stack, "", @progbits
#endif
