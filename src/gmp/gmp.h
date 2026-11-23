/* SPDX-License-Identifier: GPL-3.0-only
 * <gmp.h> for the calculator build: GMP's mini-gmp (mini-gmp.c/.h, from GMP 6.2.1, dual
 * LGPLv3/GPLv2+) plus the few functions C47 uses that mini-gmp lacks (mini-gmp-extra.c).
 * Upstream's calculator builds link the full GMP; mini-gmp is one portable C file and is fast
 * enough for C47, whose long integers are capped at a few hundred digits. */
#ifndef C47CG_GMP_H
#define C47CG_GMP_H

#include "mini-gmp.h"

#define mpz_div_2exp mpz_fdiv_q_2exp

int mpz_fits_uint_p(const mpz_t op);
void mpz_nextprime(mpz_t rop, const mpz_t op);
void mpz_fib_ui(mpz_t fn, unsigned long n);

#endif
