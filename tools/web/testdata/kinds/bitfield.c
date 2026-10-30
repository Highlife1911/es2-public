/* Bitfields in structs; ternaries, case labels and initializers are not. */
struct flags {
    unsigned ready : 1;                                            /* EXPECT: bitfield */
    unsigned mode : 3;                                             /* EXPECT: bitfield */
    unsigned : 4;                                                  /* EXPECT: bitfield */
    int plain;
};
struct table { int values[4]; } t = { { 1, 2, 3, 4 } };
int pick(int a, int b, int c) {
    switch (a) {
    case 1: return b;
    default: return c;
    }
}
int ternary(int a) { struct s { int x; } v = { a ? 1 : 2 }; return v.x; }
union word { unsigned all; unsigned char byte[4]; };
