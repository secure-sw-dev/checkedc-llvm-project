//RUN: 3c -base-dir=%S -output-postfix=checked %s %S/difftypes_xfail1.c

// XFAIL: *

// The desired behavior in this case is to fail, so other checks are omitted

int * foo(int, char *);
