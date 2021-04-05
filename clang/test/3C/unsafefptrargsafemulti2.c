// RUN: rm -rf %t*
// RUN: 3c -base-dir=%S -addcr -alltypes -output-dir=%t.checkedALL2 %S/unsafefptrargsafemulti1.c %s --
// RUN: 3c -base-dir=%S -addcr -output-dir=%t.checkedNOALL2 %S/unsafefptrargsafemulti1.c %s --
// RUN: %clang -working-directory=%t.checkedNOALL2 -c unsafefptrargsafemulti1.c unsafefptrargsafemulti2.c
// RUN: FileCheck -match-full-lines -check-prefixes="CHECK_NOALL","CHECK" --input-file %t.checkedNOALL2/unsafefptrargsafemulti2.c %s
// RUN: FileCheck -match-full-lines -check-prefixes="CHECK_ALL","CHECK" --input-file %t.checkedALL2/unsafefptrargsafemulti2.c %s
// RUN: 3c -base-dir=%S -alltypes -output-dir=%t.checked %S/unsafefptrargsafemulti1.c %s --
// RUN: 3c -base-dir=%t.checked -alltypes -output-dir=%t.convert_again %t.checked/unsafefptrargsafemulti1.c %t.checked/unsafefptrargsafemulti2.c --
// RUN: test ! -f %t.convert_again/unsafefptrargsafemulti1.c
// RUN: test ! -f %t.convert_again/unsafefptrargsafemulti2.c

/******************************************************************************/

/*This file tests three functions: two callers bar and foo, and a callee sus*/
/*In particular, this file tests: passing a function pointer as an argument to a
  function unsafely (by casting it unsafely)*/
/*For robustness, this test is identical to
unsafefptrargprotosafe.c and unsafefptrargsafe.c except in that
the callee and callers are split amongst two files to see how
the tool performs conversions*/
/*In this test, foo, bar, and sus will all treat their return values safely*/

/******************************************************************************/

#include <stddef.h>
extern _Itype_for_any(T) void *calloc(size_t nmemb, size_t size)
    : itype(_Array_ptr<T>) byte_count(nmemb * size);
extern _Itype_for_any(T) void free(void *pointer
                                   : itype(_Array_ptr<T>) byte_count(0));
extern _Itype_for_any(T) void *malloc(size_t size)
    : itype(_Array_ptr<T>) byte_count(size);
extern _Itype_for_any(T) void *realloc(void *pointer
                                       : itype(_Array_ptr<T>) byte_count(1),
                                         size_t size)
    : itype(_Array_ptr<T>) byte_count(size);
extern int printf(const char *restrict format
                  : itype(restrict _Nt_array_ptr<const char>), ...);
extern _Unchecked char *strcpy(char *restrict dest, const char *restrict src
                               : itype(restrict _Nt_array_ptr<const char>));

struct general {
  int data;
  struct general *next;
  //CHECK: _Ptr<struct general> next;
};

struct warr {
  int data1[5];
  //CHECK_NOALL: int data1[5];
  //CHECK_ALL: int data1 _Checked[5];
  char *name;
  //CHECK: _Ptr<char> name;
};

struct fptrarr {
  int *values;
  //CHECK: _Ptr<int> values;
  char *name;
  //CHECK: _Ptr<char> name;
  int (*mapper)(int);
  //CHECK: _Ptr<int (int)> mapper;
};

struct fptr {
  int *value;
  //CHECK: _Ptr<int> value;
  int (*func)(int);
  //CHECK: _Ptr<int (int)> func;
};

struct arrfptr {
  int args[5];
  //CHECK_NOALL: int args[5];
  //CHECK_ALL: int args _Checked[5];
  int (*funcs[5])(int);
  //CHECK_NOALL: int (*funcs[5])(int);
  //CHECK_ALL: _Ptr<int (int)> funcs _Checked[5];
};

static int add1(int x) {
  //CHECK: static int add1(int x) _Checked {
  return x + 1;
}

static int sub1(int x) {
  //CHECK: static int sub1(int x) _Checked {
  return x - 1;
}

static int fact(int n) {
  //CHECK: static int fact(int n) _Checked {
  if (n == 0) {
    return 1;
  }
  return n * fact(n - 1);
}

static int fib(int n) {
  //CHECK: static int fib(int n) _Checked {
  if (n == 0) {
    return 0;
  }
  if (n == 1) {
    return 1;
  }
  return fib(n - 1) + fib(n - 2);
}

static int zerohuh(int n) {
  //CHECK: static int zerohuh(int n) _Checked {
  return !n;
}

static int *mul2(int *x) {
  //CHECK: static _Ptr<int> mul2(_Ptr<int> x) _Checked {
  *x *= 2;
  return x;
}

int *sus(int (*x)(int), int (*y)(int)) {
  //CHECK_NOALL: int *sus(int (*x)(int), _Ptr<int (int)> y) : itype(_Ptr<int>) {
  //CHECK_ALL: _Array_ptr<int> sus(int (*x)(int), _Ptr<int (int)> y) : count(5) {

  x = (int (*)(int))5;
  //CHECK: x = (int (*)(int))5;
  int *z = calloc(5, sizeof(int));
  //CHECK_NOALL: int *z = calloc<int>(5, sizeof(int));
  //CHECK_ALL: _Array_ptr<int> z : count(5) = calloc<int>(5, sizeof(int));
  int i;
  for (i = 0; i < 5; i++) {
    //CHECK_NOALL: for (i = 0; i < 5; i++) {
    //CHECK_ALL: for (i = 0; i < 5; i++) _Checked {
    z[i] = y(i);
  }

  return z;
}
