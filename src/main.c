/*
** Made by fabien le mentec <texane@gmail.com>
** 
** Started on  Sat Jul 10 09:21:21 2010 texane
** Last update Sat Jul 10 16:51:50 2010 texane
*/


#include <stdio.h>
#include <sys/types.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>


#define CONFIG_USE_AFFINITY 1
#define CONFIG_USE_TICK 1


#if CONFIG_USE_TICK
#include "tick.h"
#endif


#if CONFIG_USE_AFFINITY

#include <unistd.h>
#define __USE_GNU 1
#include <sched.h>

static void set_cpu_affinity(unsigned int icpu)
{
  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);
  CPU_SET(icpu, &cpuset);
  sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset);
}

#endif


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


/* blas version */

static void mult_matrix0
(gsl_matrix* res, gsl_matrix* lhs, gsl_matrix* rhs)
{
  gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.f, lhs, rhs, 0.f, res);
}


/* sequential handwritten version */

static void mult_matrix1
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


/* parallel version */

typedef struct range
{
  size_t i;
  size_t j;
} range_t;

typedef struct work
{
  volatile long lock; /* aligned */

  /* res = lhs * rhs */
  gsl_matrix* res;
  gsl_matrix* lhs;
  gsl_matrix* rhs;

  /* remaining indices to compute */
  range_t range;
} work_t;


static inline void lock_work(work_t* w)
{
  while (__sync_lock_test_and_set(&w->lock, 0))
    ;
}

static inline void unlock_work(work_t* w)
{
  __sync_lock_release(&w->lock);
}


static inline void res_to_range(gsl_matrix* res, range_t* range)
{
  range->i = 0;
  range->j = res->size1 * res->size2;
}


static inline void index_to_res
(const gsl_matrix* res, unsigned int index, unsigned int* i, unsigned int* j)
{
  /* flat index into res ij pos */

  *i = index / res->size2;
  *j = index % res->size2;
}


#if 0 /* ununsed */
static void range_to_res
(const gsl_matrix* res, const range_t* range,
 unsigned int* imin, unsigned int* jmin,
 unsigned int* imax, unsigned int* jmax)
{
  /* flat range into res ij pos */

  index_to_res(res, range->i, imin, jmin);
  index_to_res(res, range->j, imax, jmax);
}
#endif /* unused */


static void prepare_par_work
(work_t* par_work, gsl_matrix* res, gsl_matrix* lhs, gsl_matrix* rhs)
{
  par_work->lock = 0;

  par_work->res = res;
  res_to_range(res, &par_work->range);

  par_work->lhs = lhs;
  par_work->rhs = rhs;
}

static void prepare_seq_work(work_t* seq_work, work_t* par_work)
{
  seq_work->res = par_work->res;
  seq_work->lhs = par_work->lhs;
  seq_work->rhs = par_work->rhs;
}

static int next_seq_work(work_t* seq_work, work_t* par_work)
{
  /* extract the next work */

  if (!(par_work->range.j - par_work->range.i))
    return -1;

  seq_work->range.i = par_work->range.i;
  seq_work->range.j = par_work->range.i + 1;

  ++par_work->range.i;

  return 0;
}

static void mult_matrix2
(gsl_matrix* res, gsl_matrix* lhs, gsl_matrix* rhs)
{
  work_t par_work;
  work_t seq_work;

  prepare_par_work(&par_work, res, lhs, rhs);
  prepare_seq_work(&seq_work, &par_work);

  /* extract sequential work */
  while (1)
  {
    unsigned int n;
    int res;

    lock_work(&par_work);
    res = next_seq_work(&seq_work, &par_work);
    unlock_work(&par_work);

    if (res == -1)
      break ;

    /* foreach n, compute res[index(n)] */
    for (n = seq_work.range.i; n < seq_work.range.j; ++n)
    {
      double res = 0.f;

      unsigned int i;
      unsigned int j;
      unsigned int k;

      index_to_res(seq_work.res, n, &i, &j);

      for (k = 0; k < seq_work.lhs->size2; ++k)
      {
	res +=
	  gsl_matrix_get(seq_work.lhs, i, k) *
	  gsl_matrix_get(seq_work.rhs, k, j);
      }

      gsl_matrix_set(seq_work.res, i, j, res);
    }
  }
}


/* print helpers */

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


/* select the implementation to run */

static void mult_switch(unsigned int n, gsl_matrix* res, gsl_matrix* lhs, gsl_matrix* rhs)
{
  void (*f)(gsl_matrix*, gsl_matrix*, gsl_matrix*) = NULL;

#if CONFIG_USE_TICK
  tick_counter_t ticks[3];
#endif

#define CASE_TO_F(__f, __n) case __n: __f = mult_matrix ## __n; break
  switch (n)
  {
    CASE_TO_F(f, 0);
    CASE_TO_F(f, 1);
    CASE_TO_F(f, 2);
  }

  gsl_matrix_set_zero(res);

#if CONFIG_USE_TICK
  tick_read(&ticks[0]);
#endif

  f(res, lhs, rhs);

#if CONFIG_USE_TICK
  tick_read(&ticks[1]);
#endif

#if CONFIG_USE_TICK
  tick_sub(&ticks[2], &ticks[1], &ticks[0]);
  printf("ticks: %llu\n", ticks[2].value);
#endif

  print_matrix(res);
  printf("---\n");
}


/* main */

int main(int ac, char** av)
{
  static const double lhs_data[] =
    { 0, 1, 1.f/3.f, -1 };

  static const double rhs_data[] =
    { 1, -1, 2.f/3.f, -2 };

  gsl_matrix* lhs_matrix = create_matrix_with_data((const double*)lhs_data, 2, 2);
  gsl_matrix* rhs_matrix = create_matrix_with_data((const double*)rhs_data, 2, 2);
  gsl_matrix* res_matrix = gsl_matrix_alloc(2, 2);

#if CONFIG_USE_AFFINITY
  set_cpu_affinity(1);
#endif

  mult_switch(0, res_matrix, lhs_matrix, rhs_matrix);
  mult_switch(1, res_matrix, lhs_matrix, rhs_matrix);
  mult_switch(2, res_matrix, lhs_matrix, rhs_matrix);

  gsl_matrix_free(lhs_matrix);
  gsl_matrix_free(rhs_matrix);
  gsl_matrix_free(res_matrix);

  return 0;
}
