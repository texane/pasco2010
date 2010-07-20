/*
** Made by fabien le mentec <texane@gmail.com>
** 
** Started on  Sat Jul 10 09:21:21 2010 texane
** Last update Sun Jul 18 14:48:54 2010 texane
*/


#include <stdio.h>
#include <sys/types.h>
#include "matrix.h"


#define CONFIG_DO_TIMES 1
#define CONFIG_PRINT_RES 0
#define CONFIG_ITER_COUNT 4
#define CONFIG_DO_CHECK 1
#define CONFIG_DO_DEALLOC 0


#if CONFIG_DO_TIMES
#include <sys/time.h>
static inline double get_elapsed_time(void)
{
  struct timeval tv;
  if (gettimeofday(&tv, 0)) return 0;
  return (double)tv.tv_sec + 1e-6*(double)tv.tv_usec;
}
#endif


#if 0 /* print helpers */

static inline void print_double(double value)
{
  if (value >= 0.f)
    printf(" ");
  printf("%g ", value);
}


static void __attribute__((unused)) print_matrix(const gsl_matrix* m)
{
  size_t i, j;

  for (i = 0; i < m->size1; ++i)
  {
    for (j = 0; j < m->size2; ++j)
      print_double(gsl_matrix_get(m, i, j));
    printf("\n");
  }
}

#endif


/* select and time the implementation to run */

void mul_matrix_0(matrix_t*, const matrix_t*, const matrix_t*);
void mul_matrix_1(matrix_t*, const matrix_t*, const matrix_t*);

static void mul_switch
(unsigned int n, matrix_t* res, const matrix_t* lhs, const matrix_t* rhs)
{
  static void (*f)(matrix_t*, const matrix_t*, const matrix_t*) = NULL;

#if CONFIG_DO_TIMES
  double times[2];
#endif

#define CASE_TO_F(__f, __n) case __n: __f = mul_matrix_ ## __n; break
  switch (n)
  {
    CASE_TO_F(f, 0);
    CASE_TO_F(f, 1);
  }

#if CONFIG_DO_TIMES
  times[0] = get_elapsed_time();
#endif

  f(res, lhs, rhs);

#if CONFIG_DO_TIMES
  times[1] = get_elapsed_time();
  printf("time: %lf\n", times[1] - times[0]);
#endif

#if CONFIG_PRINT_RES
  print_matrix(res);
  printf("---\n");
#endif
}


/* main */

int main(int ac, char** av)
{
  matrix_t* lhs = NULL;
  matrix_t* rhs = NULL;
  matrix_t* res = NULL;
  matrix_t* tmp = NULL;

  unsigned int i;

  if (ac < 4)
    goto on_error;

  if (matrix_load_file(&lhs, av[1]) == -1)
    goto on_error;

  if (matrix_load_file(&rhs, av[2]) == -1)
    goto on_error;

  if (matrix_create(&res, lhs->size1, rhs->size2) == -1)
    goto on_error;

#if !CONFIG_DO_CHECK
  if (ac == 5)
#endif
    if (matrix_create(&tmp, lhs->size1, rhs->size2) == -1)
      goto on_error;

  for (i = 0; i < CONFIG_ITER_COUNT; ++i)
  {
#if !CONFIG_DO_CHECK
    if (tmp != NULL)
#endif
      mul_switch(0, tmp, lhs, rhs);

    mul_switch(1, res, lhs, rhs);

#if CONFIG_DO_CHECK
    if (matrix_cmp(tmp, res))
      printf("invalid\n");
#endif

    printf("--\n");
  }

  /* store the last ones */
  if (ac == 5)
    matrix_store_file(tmp, av[4]);
  matrix_store_file(res, av[3]);

 on_error:

#if CONFIG_DO_DEALLOC
  if (res != NULL)
    matrix_destroy(res);
  if (tmp != NULL)
    matrix_destroy(tmp);
  if (lhs != NULL)
    matrix_destroy(lhs);
  if (rhs != NULL)
    matrix_destroy(rhs);
#endif

  return 0;
}
