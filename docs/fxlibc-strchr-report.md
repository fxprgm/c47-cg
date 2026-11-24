# strchr() and strrchr() never match the terminating null, and miss bytes >= 0x80

(Draft of an issue for fxlibc, https://git.planet-casio.com/Vhex-Kernel-Core/fxlibc)

In master (e2f458f, 1.5.1) and dev (b96335c), `src/string/strchr.c` and `strrchr.c` have two
problems; `strchrnul.c` has the second.

**1. The terminating null is never matched.** C (C99/C11 7.24.5.2) says "The terminating null
character is considered to be part of the string", so `strchr(s, '\0')` returns a pointer to
the end of `s` (a common idiom for appending), and `strrchr(s, '\0')` too. The loop stops
before comparing the null, so both return NULL.

**2. Bytes >= 0x80 are never found when passed as an `unsigned char` value.** The standard
converts `c` to `char` before comparing. `s[i] == c` compares a `char` (signed on sh-elf, so
-96 for 0xA0) with the `int` 160, which is never equal. `strchr(s, 0xA0)` or
`strchr(s, (unsigned char)x)` misses the byte.

```c
#include <string.h>
#include <assert.h>

char s[] = "ab\xa0";
assert(strchr(s, '\0') == s + 3);     /* fxlibc: NULL  */
assert(strrchr(s, '\0') == s + 3);    /* fxlibc: NULL  */
assert(strchr(s, 0xa0) == s + 2);     /* fxlibc: NULL  */
assert(strrchr(s, 0xa0) == s + 2);    /* fxlibc: NULL  */
assert(strchrnul(s, 0xa0) == s + 2);  /* fxlibc: s + 3 */
```

(`memchr` is fine: the SH version sign-extends `c`.)

Found while porting the C47 calculator firmware to the fx-CG50 as a gint add-in: it appends a
complex number's imaginary part at `strchr(str, '\0')`, which with fxlibc wrote through a NULL
pointer and dropped the digits. The add-in now carries its own versions:

```c
char *strchr(const char *s, int c) {
  const char ch = (char)c;
  for(;; s++) {
    if(*s == ch) return (char *)s;
    if(*s == '\0') return NULL;
  }
}

char *strrchr(const char *s, int c) {
  const char ch = (char)c;
  const char *last = NULL;
  for(;; s++) {
    if(*s == ch) last = s;
    if(*s == '\0') return (char *)last;
  }
}

char *strchrnul(const char *s, int c) {
  const char ch = (char)c;
  while(*s != '\0' && *s != ch) s++;
  return (char *)s;
}
```
