#include <stdio.h>
#include <sys/types.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>


static gsl_matrix* create_matrix_with_data
(const double* data, size_t nrow, size_t ncol)
{
  gsl_matrix* const m = gsl_matrix_alloc(nrow, ncol);
  size_t i, j;

  for (i = 0; i < nrow; ++i)
    for (j = 0; j < ncol; ++j)
      gsl_matrix_set(m, i, j, data[i * ncol + j]);

  return m;
}


static void mult_matrix2
(gsl_matrix* res, gsl_matrix* lhs, gsl_matrix* rhs)
{
  gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.f, lhs, rhs, 0.f, res);
}


static void mult_matrix
(gsl_matrix* res, gsl_matrix* lhs, gsl_matrix* rhs)
{
  size_t i, j, k;

  for (i = 0; i < lhs->size1; ++i)
  {
    for (j = 0; j < lhs->size2; ++j)
    {
      double t = 0.f;
      for (k = 0; k < rhs->size1; ++k)
	t += gsl_matrix_get(lhs, i, k) * gsl_matrix_get(rhs, k, j);
      gsl_matrix_set(res, i, j, t);
    }
  }
}


static inline void print_double(double value)
{
  if (value >= 0.f)
    printf(" ");
  printf("%g ", value);
}


static void print_matrix(const gsl_matrix* m)
{
  size_t i, j;

  for (i = 0; i < m->size1; ++i)
  {
    for (j = 0; j < m->size2; ++j)
      print_double(gsl_matrix_get(m, i, j));
    printf("\n");
  }
}


int main(int ac, char** av)
{
  static const double lhs_data[] =
    { 0, 1, 1.f/3.f, -1 };

  static const double rhs_data[] =
    { 1, -1, 2.f/3.f, -2 };

  gsl_matrix* lhs_matrix = create_matrix_with_data((const double*)lhs_data, 2, 2);
  gsl_matrix* rhs_matrix = create_matrix_with_data((const double*)rhs_data, 2, 2);
  gsl_matrix* res_matrix = gsl_matrix_alloc(2, 2);

  mult_matrix2(res_matrix, lhs_matrix, rhs_matrix);
  print_matrix(res_matrix);
  printf("---\n");

  mult_matrix(res_matrix, lhs_matrix, rhs_matrix);
  print_matrix(res_matrix);
  printf("---\n");

  gsl_matrix_free(lhs_matrix);
  gsl_matrix_free(rhs_matrix);
  gsl_matrix_free(res_matrix);

  return 0;
}
