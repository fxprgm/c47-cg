/* SPDX-License-Identifier: GPL-3.0-only
 * The GMP functions C47 uses that mini-gmp doesn't have (see gmp.h). */
#include <limits.h>
#include "gmp.h"

int mpz_fits_uint_p(const mpz_t op) {
  return mpz_sgn(op) >= 0 && mpz_cmp_ui(op, UINT_MAX) <= 0;
}


/* The smallest prime > op, as GMP's (whose primality test is also probabilistic). */
void mpz_nextprime(mpz_t rop, const mpz_t op) {
  if(mpz_cmp_ui(op, 2) < 0) {
    mpz_set_ui(rop, 2);
    return;
  }
  mpz_add_ui(rop, op, 1);
  if(mpz_even_p(rop) && mpz_cmp_ui(rop, 2) != 0) {
    mpz_add_ui(rop, rop, 1);
  }
  while(!mpz_probab_prime_p(rop, 25)) {
    mpz_add_ui(rop, rop, 2);
  }
}


/* F(n) by fast doubling: F(2k) = F(k)(2F(k+1) - F(k)), F(2k+1) = F(k)^2 + F(k+1)^2. */
void mpz_fib_ui(mpz_t fn, unsigned long n) {
  mpz_t a, b, t, u;
  mpz_init_set_ui(a, 0);              /* F(k)   */
  mpz_init_set_ui(b, 1);              /* F(k+1) */
  mpz_init(t);
  mpz_init(u);
  int bit = 0;
  for(unsigned long m = n; m; m >>= 1) {
    bit++;
  }
  while(bit-- > 0) {
    mpz_mul_2exp(t, b, 1);
    mpz_sub(t, t, a);
    mpz_mul(t, t, a);                 /* F(2k)   */
    mpz_mul(u, a, a);
    mpz_addmul(u, b, b);              /* F(2k+1) */
    if((n >> bit) & 1) {
      mpz_add(b, t, u);               /* F(2k+2) */
      mpz_swap(a, u);
    }
    else {
      mpz_swap(a, t);
      mpz_swap(b, u);
    }
  }
  mpz_swap(fn, a);
  mpz_clear(a);
  mpz_clear(b);
  mpz_clear(t);
  mpz_clear(u);
}
