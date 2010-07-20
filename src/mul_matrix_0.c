#include "matrix.h"


/* sequential handwritten version */

void mul_matrix_0(matrix_t* res, const matrix_t* lhs, const matrix_t* rhs)
{
  size_t i, j, k;

  for (i = 0; i < lhs->size1; ++i)
  {
    for (j = 0; j < lhs->size2; ++j)
    {
      /* e = 0; */
      matrix_elem_t* const e = matrix_at(res, i, j);
      matrix_elem_init(*e);

      for (k = 0; k < rhs->size1; ++k)
      {
	/* e += l * r; */
	const matrix_elem_t* const l = matrix_const_at(lhs, i, k);
	const matrix_elem_t* const r = matrix_const_at(rhs, k, j);
	matrix_elem_addmul(*e, *l, *r);
      }
    }
  }
}
