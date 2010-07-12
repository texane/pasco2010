/*
** Made by fabien le mentec <texane@gmail.com>
** 
** Started on  Sat Jul 10 09:21:21 2010 texane
** Last update Mon Jul 12 04:04:00 2010 fabien le mentec
*/


#include <stdio.h>
#include <sys/types.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>
#include "kaapi.h"


#define CONFIG_USE_TICK 1
#define CONFIG_USE_WORKLOAD 1
#define CONFIG_USE_DEBUG 0

#define CONFIG_ADD_SEQLOAD 0

#define CONFIG_PAR_SIZE 1
#define CONFIG_SEQ_SIZE 1

#define CONFIG_PRINT_RES 0

#define CONFIG_ITER_COUNT 10


#if CONFIG_USE_TICK
#include "tick.h"
#endif


#if CONFIG_ADD_SEQLOAD
#include <unistd.h>

static void add_seq_load(void)
{
  usleep(10000);
}

#endif


#if CONFIG_USE_WORKLOAD
extern void kaapi_set_workload(struct kaapi_processor_t*, kaapi_uint32_t);
extern void kaapi_set_self_workload(kaapi_uint32_t);
extern struct kaapi_processor_t* kaapi_stealcontext_kproc(kaapi_stealcontext_t*);
extern struct kaapi_processor_t* kaapi_request_kproc(kaapi_request_t*);
extern unsigned int kaapi_request_kid(kaapi_request_t*);
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


/* xkaapi parallel version */

typedef struct range
{
  unsigned int i;
  unsigned int j;
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

  /* xkaapi related */
  kaapi_stealcontext_t* master_sc;
  kaapi_taskadaptive_result_t* ktr;
} work_t __attribute__((aligned(64)));


static inline void lock_work(work_t* w)
{
  while (__sync_lock_test_and_set(&w->lock, 0))
    ;
}

static inline void unlock_work(work_t* w)
{
  __sync_lock_release(&w->lock);
}


static inline unsigned int get_range_size(const range_t* range)
{
  return range->j - range->i;
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
(work_t* par_work, gsl_matrix* res,
 gsl_matrix* lhs, gsl_matrix* rhs,
 const range_t* range,
 kaapi_stealcontext_t* master_sc,
 kaapi_taskadaptive_result_t* ktr)
{
  par_work->lock = 0;

  par_work->res = res;
  par_work->range = *range;

  par_work->lhs = lhs;
  par_work->rhs = rhs;

  par_work->master_sc = master_sc;
  par_work->ktr = ktr;
}

static void prepare_seq_work(work_t* seq_work, work_t* par_work)
{
  seq_work->res = par_work->res;
  seq_work->lhs = par_work->lhs;
  seq_work->rhs = par_work->rhs;
}

static int next_seq_work(work_t* seq_work, work_t* par_work)
{
  /* extract sequential work */

  const unsigned int range_size = get_range_size(&par_work->range);
  const unsigned int seq_size = range_size < CONFIG_SEQ_SIZE ?
    range_size : CONFIG_SEQ_SIZE;

  if (!seq_size)
    return -1;

  seq_work->range.i = par_work->range.i;
  seq_work->range.j = par_work->range.i + seq_size;
  par_work->range.i += seq_size;

  return 0;
}

static inline void steal_range
(range_t* stolen_range, unsigned int steal_size, range_t* victim_range)
{
  stolen_range->j = victim_range->j;
  stolen_range->i = victim_range->j - steal_size;
  victim_range->j -= steal_size;
}

static int reduce_function
(kaapi_stealcontext_t* sc, void* targ, void* tptr, size_t tsize, void* vptr)
{
  work_t* const vwork = (work_t*)vptr;
  work_t* const twork = (work_t*)tptr;

  /* lock vwork since may be in a splitting process */
  lock_work(vwork);
  vwork->range.i = twork->range.i;
  vwork->range.j = twork->range.j;
#if CONFIG_USE_WORKLOAD
  kaapi_set_self_workload(get_range_size(&vwork->range));
#endif
  unlock_work(vwork);

  return 0; /* false, continue */
}

/* forward decl */
static void task_entry(void*, kaapi_thread_t*);

static int split_function
(kaapi_stealcontext_t* sc, int request_count,
 kaapi_request_t* request, void* arg)
{
  work_t* const vwork = (work_t*)arg;
  range_t stolen_range;
  range_t thief_range;

  /* steal a balanced part of the victim range */
  unsigned int has_stolen = 0;
  lock_work(vwork);

  /* compute unit size */
  const unsigned int range_size = get_range_size(&vwork->range);
  size_t unit_size = 0;

  if (range_size == 0)
    goto dont_steal;

  unit_size = range_size / (request_count + 1);
  if (unit_size < CONFIG_PAR_SIZE)
  {
    request_count = (range_size / CONFIG_PAR_SIZE) - 1;
    if (request_count <= 0)
      goto dont_steal;
    unit_size = CONFIG_PAR_SIZE;
  }

  /* steal the range */
  const size_t steal_size = unit_size * (size_t)request_count;
  steal_range(&stolen_range, steal_size, &vwork->range);

#if CONFIG_USE_WORKLOAD
  /* update victim workload */
  kaapi_set_workload
    (kaapi_stealcontext_kproc(sc), get_range_size(&vwork->range));
#endif

  has_stolen = 1;

 dont_steal:
  unlock_work(vwork);

  if (!has_stolen)
    return 0;

#if CONFIG_USE_DEBUG
  printf("steal_size %lu\n", steal_size);
#endif

  /* reply requests */

  int reply_count = 0;

  for (; request_count > 0; ++request)
  {
    if (!kaapi_request_ok(request))
      continue;

    /* pop no more than unit_size */
    if (unit_size > (unsigned int)get_range_size(&stolen_range))
      unit_size = get_range_size(&stolen_range);
    steal_range(&thief_range, unit_size, &stolen_range);

    kaapi_thread_t* thief_thread = kaapi_request_getthread(request);
    kaapi_task_t* thief_task = kaapi_thread_toptask(thief_thread);

    /* allocate task stack */
    work_t* twork = (work_t*)
      (kaapi_thread_pushdata_align(thief_thread, sizeof(work_t), 8));

    /* allocate task result */
    kaapi_taskadaptive_result_t* const ktr =
      kaapi_allocate_thief_result(sc, sizeof(work_t), NULL);

    /* initialize task stack */
    prepare_par_work
      (twork, vwork->res, vwork->lhs, vwork->rhs, &thief_range, sc, ktr);

    kaapi_task_init(thief_task, task_entry, twork);
    kaapi_thread_pushtask(thief_thread);
    kaapi_request_reply_head(sc, request, ktr);

#if CONFIG_USE_WORKLOAD
    kaapi_set_workload(kaapi_request_kproc(request), unit_size);
#endif

    --request_count;
    ++reply_count;
  }

  return reply_count;
} /* split_function */


static void task_entry(void* arg, kaapi_thread_t* thread)
{
  work_t* const par_work = (work_t*)arg;
  work_t seq_work;

#if CONFIG_USE_DEBUG
  printf("task_entry [%u - %u[\n", par_work->range.i, par_work->range.j);
#endif

  /* push the adaptive task */
  const int sc_flags = (par_work->master_sc == NULL) ?
    KAAPI_STEALCONTEXT_DEFAULT : KAAPI_STEALCONTEXT_LINKED;
  kaapi_stealcontext_t* const sc = kaapi_thread_pushstealcontext
    (thread, sc_flags, split_function, arg, par_work->master_sc);

  /* prepare sequential work once */
  prepare_seq_work(&seq_work, par_work);

  /* extract sequential work */
 redo_work:
  while (1)
  {
    unsigned int n;
    int res;

    lock_work(par_work);
    res = next_seq_work(&seq_work, par_work);
    unlock_work(par_work);

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
#if CONFIG_ADD_SEQLOAD
	add_seq_load();
#endif

	res +=
	  gsl_matrix_get(seq_work.lhs, i, k) *
	  gsl_matrix_get(seq_work.rhs, k, j);
      }

      gsl_matrix_set(seq_work.res, i, j, res);
    }

    /* check for preemption */
    if (par_work->ktr != NULL)
    {
      const int is_preempted = kaapi_preemptpoint
	(par_work->ktr, sc, NULL, NULL, par_work, sizeof(work_t), NULL);
      if (is_preempted)
	return ;
    }
  }

  /* thief preemption */
  kaapi_taskadaptive_result_t* const ktr = kaapi_get_thief_head(sc);
  if (ktr != NULL)
  {
    kaapi_preempt_thief(sc, ktr, NULL, reduce_function, par_work);
    goto redo_work;
  }

  kaapi_steal_finalize(sc);
}

static void mult_matrix2
(gsl_matrix* res, gsl_matrix* lhs, gsl_matrix* rhs)
{
  work_t par_work;
  kaapi_thread_t* thread;
  kaapi_task_t* task;
  kaapi_frame_t frame;

  range_t range;
  res_to_range(res, &range);
  prepare_par_work(&par_work, res, lhs, rhs, &range, NULL, NULL);

  /* create and run main task */
#if CONFIG_USE_WORKLOAD
  kaapi_set_self_workload(get_range_size(&par_work.range));
#endif
  thread = kaapi_self_thread();
  kaapi_thread_save_frame(thread, &frame);
  task = kaapi_thread_toptask(thread);
  kaapi_task_init(task, task_entry, &par_work);
  kaapi_thread_pushtask(thread);
  kaapi_sched_sync();
  kaapi_thread_restore_frame(thread, &frame);
#if CONFIG_USE_WORKLOAD
  kaapi_set_self_workload(0);
#endif
}


/* print helpers */

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
  printf("ticks: %lu\n", ticks[2].value);
#endif

#if CONFIG_PRINT_RES
  print_matrix(res);
  printf("---\n");
#endif
}


/* main */

int main(int ac, char** av)
{
#define DIM 20

  static const double lhs_data[] =
  {
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2,
    0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2, 0, 1, 1.f/3.f, 3, 2, 1, 2, 2, 2, 2
  };

  static const double rhs_data[] =
  {
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3,
    1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3, 1, -1, 2.f/3.f, 2, 3, 1, 3, 3, 3, 3
  };

  gsl_matrix* lhs_matrix = create_matrix_with_data
    ((const double*)lhs_data, DIM, DIM);
  gsl_matrix* rhs_matrix = create_matrix_with_data
    ((const double*)rhs_data, DIM, DIM);
  gsl_matrix* res_matrix = gsl_matrix_alloc(DIM, DIM);

  unsigned int i;
  for (i = 0; i < CONFIG_ITER_COUNT; ++i)
  {
    mult_switch(0, res_matrix, lhs_matrix, rhs_matrix);
    mult_switch(1, res_matrix, lhs_matrix, rhs_matrix);
    mult_switch(2, res_matrix, lhs_matrix, rhs_matrix);
    printf("--\n");
  }

  gsl_matrix_free(lhs_matrix);
  gsl_matrix_free(rhs_matrix);
  gsl_matrix_free(res_matrix);

  return 0;
}
