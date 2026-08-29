/* lcl-random: xoshiro128** pseudo-random streams as opaque handles.
 *
 * Not cryptographically secure. Use Crypto::random_bytes for keys,
 * tokens, nonces, salts, or anything security-sensitive.
 *
 * Strict C89: there is no 32-bit integer type, so the generator runs
 * on `unsigned long` (>= 32 bits) with every intermediate masked back
 * to 32 bits. On a 64-bit `unsigned long` an unmasked shift or
 * multiply would silently turn this into a different generator; the
 * known-answer tests in test/test.lcl pin the output against the
 * canonical uint32_t implementation.
 *
 * Seed mapping, int mapping and float construction are part of the
 * compatibility contract: `Xoshiro::new 42` must yield the same
 * sequence in every future version. Details next to each function. */

#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <lcl.h>

#define RANDOM_NS "Xoshiro"
#define XOSHIRO_TYPE "xoshiro128**"
#define U32_MASK 0xffffffffUL

typedef unsigned long u32;

struct xoshiro {
  u32 s[4];
};

static u32 rotl32(u32 x, int k) {
  x &= U32_MASK;
  return ((x << k) | (x >> (32 - k))) & U32_MASK;
}

/* xoshiro128** 1.0 (Blackman & Vigna, 2018), public domain. */
static u32 xoshiro_next(struct xoshiro *r) {
  u32 *s = r->s;
  u32 result = (rotl32((s[1] * 5UL) & U32_MASK, 7) * 9UL) & U32_MASK;
  u32 t = (s[1] << 9) & U32_MASK;

  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = rotl32(s[3], 11);

  return result;
}

static u32 mix32(u32 *st) {
  u32 z;

  *st = (*st + 0x9e3779b9UL) & U32_MASK;
  z = *st;
  z = ((z ^ (z >> 16)) * 0x21f0aaadUL) & U32_MASK;
  z = ((z ^ (z >> 15)) * 0x735a2d97UL) & U32_MASK;

  return (z ^ (z >> 15)) & U32_MASK;
}

static void xoshiro_seed(struct xoshiro *r, u32 seed) {
  u32 st = seed & U32_MASK;
  int i;

  for (i = 0; i < 4; i++) {
    r->s[i] = mix32(&st);
  }

  if ((r->s[0] | r->s[1] | r->s[2] | r->s[3]) == 0) {
    r->s[0] = 1;
  }
}

static u32 default_seed(void) {
  static unsigned long counter = 0;
  u32 st = (u32)time(NULL) & U32_MASK;

  st ^= ((u32)clock() << 16) & U32_MASK;
  st ^= (++counter * 0x9e3779b9UL) & U32_MASK;

  return mix32(&st);
}

static lcl_return_code get_rng(lcl_interp *interp, const char *who,
                               lcl_value *v, struct xoshiro **out) {
  if (lcl_opaque_get(v, XOSHIRO_TYPE, (void **)out) != LCL_OK) {
    char msg[128];

    strcpy(msg, who);
    strcat(msg, ": expected xoshiro stream (from Xoshiro::new)");
    lcl_set_error(interp, msg);
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

static unsigned long uniform_below(struct xoshiro *r, unsigned long width) {
  int k = 0;
  unsigned long tmp = width;
  unsigned long cand;

  while (tmp) {
    k++;
    tmp >>= 1;
  }

  if (k == 0) {
    return 0;
  }

  for (;;) {
    if (k <= 32) {
      cand = xoshiro_next(r) >> (32 - k);
    } else {
      unsigned long hi = xoshiro_next(r);
      unsigned long lo = xoshiro_next(r);
      unsigned long mask = (k >= (int)(sizeof(unsigned long) * CHAR_BIT))
                               ? ULONG_MAX
                               : ((1UL << k) - 1UL);

      cand = (((hi << 16) << 16) | lo) & mask;
    }

    if (cand <= width) {
      return cand;
    }
  }
}

/* Xoshiro::new ?seed? -> stream */
static lcl_return_code c_new(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct xoshiro *r;
  u32 seed;

  if (argc > 1) {
    lcl_set_error(interp, "Xoshiro::new: expected 0 or 1 arguments");
    return LCL_RC_ERR;
  }

  if (argc == 1) {
    long s;

    if (lcl_value_to_int(argv[0], &s) != LCL_OK) {
      lcl_set_error(interp, "Xoshiro::new: seed must be an integer");
      return LCL_RC_ERR;
    }

    seed = (u32)(unsigned long)s & U32_MASK;
  } else {
    seed = default_seed();
  }

  r = (struct xoshiro *)calloc(1, sizeof(*r));

  if (!r) {
    lcl_set_error(interp, "Xoshiro::new: out of memory");
    return LCL_RC_ERR;
  }

  xoshiro_seed(r, seed);
  *out = lcl_opaque_new(r, XOSHIRO_TYPE, free);

  if (!*out) {
    free(r);
    lcl_set_error(interp, "Xoshiro::new: out of memory");
    return LCL_RC_ERR;
  }

  return LCL_RC_OK;
}

/* Xoshiro::int stream lo hi -> integer in [lo, hi] (inclusive) */
static lcl_return_code c_int(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  struct xoshiro *r;
  long lo;
  long hi;
  unsigned long width;
  unsigned long off;

  if (argc != 3) {
    lcl_set_error(interp, "Xoshiro::int: expected 3 arguments (stream lo hi)");
    return LCL_RC_ERR;
  }

  if (get_rng(interp, "Xoshiro::int", argv[0], &r) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_to_int(argv[1], &lo) != LCL_OK ||
      lcl_value_to_int(argv[2], &hi) != LCL_OK) {
    lcl_set_error(interp, "Xoshiro::int: lo and hi must be integers");
    return LCL_RC_ERR;
  }

  if (lo > hi) {
    lcl_set_error(interp, "Xoshiro::int: lo must be <= hi");
    return LCL_RC_ERR;
  }

  width = (unsigned long)hi - (unsigned long)lo;
  off = uniform_below(r, width);
  *out = lcl_int_new((long)((unsigned long)lo + off));

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* Xoshiro::float stream -> float in [0, 1) */
static lcl_return_code c_float(lcl_interp *interp, int argc, lcl_value **argv,
                               lcl_value **out) {
  struct xoshiro *r;
  double a;
  double b;
  double x;

  if (argc != 1) {
    lcl_set_error(interp, "Xoshiro::float: expected 1 argument (stream)");
    return LCL_RC_ERR;
  }

  if (get_rng(interp, "Xoshiro::float", argv[0], &r) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  a = (double)(xoshiro_next(r) >> 5);
  b = (double)(xoshiro_next(r) >> 6);
  x = (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);

  if (x >= 1.0) {
    x = 1.0 - DBL_EPSILON;
  }

  *out = lcl_float_new(x);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* Xoshiro::shuffle stream list -> new list, Fisher-Yates */
static lcl_return_code c_shuffle(lcl_interp *interp, int argc,
                                 lcl_value **argv, lcl_value **out) {
  struct xoshiro *r;
  lcl_value **elems;
  lcl_value *result;
  size_t n;
  size_t i;

  if (argc != 2) {
    lcl_set_error(interp,
                  "Xoshiro::shuffle: expected 2 arguments (stream list)");
    return LCL_RC_ERR;
  }

  if (get_rng(interp, "Xoshiro::shuffle", argv[0], &r) != LCL_RC_OK) {
    return LCL_RC_ERR;
  }

  if (lcl_value_type_of(argv[1]) != LCL_LIST) {
    lcl_set_error(interp, "Xoshiro::shuffle: expected list");
    return LCL_RC_ERR;
  }

  n = lcl_list_len(argv[1]);
  elems = (lcl_value **)calloc(n ? n : 1, sizeof(*elems));

  if (!elems) {
    lcl_set_error(interp, "Xoshiro::shuffle: out of memory");
    return LCL_RC_ERR;
  }

  for (i = 0; i < n; i++) {
    if (lcl_list_get(argv[1], i, &elems[i]) != LCL_OK) {
      while (i > 0) {
        lcl_ref_dec(elems[--i]);
      }

      free(elems);
      lcl_set_error(interp, "Xoshiro::shuffle: internal error reading list");
      return LCL_RC_ERR;
    }
  }

  for (i = n; i > 1; i--) {
    size_t j = (size_t)uniform_below(r, (unsigned long)(i - 1));
    lcl_value *t = elems[i - 1];

    elems[i - 1] = elems[j];
    elems[j] = t;
  }

  result = lcl_list_new();

  for (i = 0; i < n; i++) {
    if (result && lcl_list_push(&result, elems[i]) != LCL_OK) {
      lcl_ref_dec(result);
      result = NULL;
    }

    lcl_ref_dec(elems[i]);
  }

  free(elems);

  if (!result) {
    lcl_set_error(interp, "Xoshiro::shuffle: out of memory");
    return LCL_RC_ERR;
  }

  *out = result;

  return LCL_RC_OK;
}

void lcl_register_random(lcl_interp *interp) {
  lcl_value *ns = lcl_ns_new(RANDOM_NS);

  lcl_define_take(interp, RANDOM_NS, ns);

  lcl_ns_def_take(ns, "new", lcl_c_proc_new("Xoshiro::new", c_new));
  lcl_ns_def_take(ns, "int", lcl_c_proc_new("Xoshiro::int", c_int));
  lcl_ns_def_take(ns, "float", lcl_c_proc_new("Xoshiro::float", c_float));
  lcl_ns_def_take(ns, "shuffle",
                  lcl_c_proc_new("Xoshiro::shuffle", c_shuffle));
}
