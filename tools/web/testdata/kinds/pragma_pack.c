/* Packed layouts: #pragma pack is forbidden, the attribute is informational. */
#pragma once
#pragma pack(push, 1)                                              /* EXPECT: pragma-pack */
struct wire { unsigned char a; unsigned b; };
#pragma pack(pop)                                                  /* EXPECT: pragma-pack */
struct attr { char a; int b; } __attribute__((packed));            /* EXPECT: packed-attr */
struct aligned { char a; int b; } __attribute__((aligned(4)));
/* #pragma pack(1) in a comment */
