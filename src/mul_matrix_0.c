#include "matrix.h"


/* sequential handwritten version */

extern void innerprod
(
 matrix_elem_t res,
 const matrix_t* a,
 const size_t ai,
 const matrix_t* b,
 const size_t bj
);

void mul_matrix_0(matrix_t* res, const matrix_t* lhs, const matrix_t* rhs)
{
  size_t i, j;

  for (i = 0; i < lhs->size1; ++i)
  {
    for (j = 0; j < lhs->size2; ++j)
    {
      /* e = 0; */
      matrix_elem_t* const e = matrix_at(res, i, j);
      matrix_elem_init(*e);

#if 0
      for (size_t k = 0; k < rhs->size1; ++k)
      {
	/* e += l * r; */
	const matrix_elem_t* const l = matrix_const_at(lhs, i, k);
	const matrix_elem_t* const r = matrix_const_at(rhs, k, j);
	matrix_elem_addmul(*e, *l, *r);
      }
#else
      innerprod(*e, lhs, i, rhs, j);
#endif
    }
  }
}

void matrix_mul_0_linbox_wrapper
(
 size_t m, size_t n, size_t k,
 matrix_elem_t* a, const size_t lda,
 matrix_elem_t* b, const size_t ldb,
 matrix_elem_t* c, const size_t ldc
)
{
#define MATRIX_INIT(__ld, __size1, __size2, __data) \
  { __ld, __size1, __size2, __data }

  matrix_t res = MATRIX_INIT(ldc, m, n, c);
  matrix_t lhs = MATRIX_INIT(lda, m, k, a);
  matrix_t rhs = MATRIX_INIT(ldb, k, n, b);

  mul_matrix_0(&res, &lhs, &rhs);
}
