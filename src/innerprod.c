#include <sys/types.h>
#include "kaapi.h"
#include "matrix.h"

extern void kaapi_set_workload(struct kaapi_processor_t*, kaapi_uint32_t);
extern void kaapi_set_self_workload(kaapi_uint32_t);
extern struct kaapi_processor_t* kaapi_stealcontext_kproc(kaapi_stealcontext_t*);
extern struct kaapi_processor_t* kaapi_request_kproc(kaapi_request_t*);
extern unsigned int kaapi_request_kid(kaapi_request_t*);

typedef struct range
{
  unsigned int i;
  unsigned int j;
} range_t;

static inline unsigned int get_range_size(const range_t* range)
{
  return range->j - range->i;
}


static inline void dim_to_range(matrix_t* res, range_t* range)
{
  range->i = 0;
  range->j = res->size1 * res->size2;
}

typedef struct work
{
  volatile long lock; /* aligned */

  const matrix_t* a;
  size_t ai;

  const matrix_t* b;
  size_t bj;

  range_t range;

  matrix_elem_t res;

  /* xkaapi related */
  kaapi_stealcontext_t* master_sc;
  kaapi_taskadaptive_result_t* ktr;
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

static void prepare_par_work
(work_t* par_work, 
 const matrix_t* a, size_t ai,
 const matrix_t* b, size_t bj,
 kaapi_stealcontext_t* master_sc,
 kaapi_taskadaptive_result_t* ktr)
{
  par_work->lock = 0;

  par_work->a = a;
  par_work->ai = ai;

  par_work->b = b;
  par_work->bj = bj;

  par_work->range.i = 0;
  par_work->range.j = a->size1; /* row count */

  matrix_elem_init(par_work->res);

  par_work->master_sc = master_sc;
  par_work->ktr = ktr;
}


/* static void prepare_seq_work(work_t* seq_work, work_t* par_work) */
/* { */
/*   seq_work->a = par_work->a; */
/*   seq_work->ai = par_work->ai; */
/*   seq_work->b = par_work->b; */
/*   seq_work->bj = par_work->bj; */
/* } */


static int next_seq_work(work_t* seq_work, work_t* par_work)
{
#define CONFIG_SEQ_SIZE 1

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

#if CONFIG_USE_DEBUG
  printf("> reduce [%u - %u[\n", twork->range.i, twork->range.j);
#endif

  /* lock vwork since may be in a splitting process */
  lock_work(vwork);

  /* reduce result */
  matrix_elem_add(vwork->res, twork->res);
  vwork->range.i = twork->range.i;
  vwork->range.j = twork->range.j;

  kaapi_set_self_workload(get_range_size(&vwork->range));

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

  /* steal a balanced part of the victim range */
  unsigned int has_stolen = 0;
  lock_work(vwork);

  /* compute unit size */
  const unsigned int range_size = get_range_size(&vwork->range);
  size_t unit_size = 0;

  if (range_size == 0)
    goto dont_steal;

#define CONFIG_PAR_SIZE 1
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

  /* update victim workload */
  kaapi_set_workload
    (kaapi_stealcontext_kproc(sc), get_range_size(&vwork->range));

  has_stolen = 1;

 dont_steal:
  unlock_work(vwork);

  if (!has_stolen)
    return 0;

#if CONFIG_USE_DEBUG
  printf("unit_size == %lu, %d\n", unit_size, request_count);
  printf("steal_size %lu\n", steal_size);
#endif

  /* reply requests */

  int reply_count = 0;

  for (; request_count > 0; ++request)
  {
    if (!kaapi_request_ok(request))
      continue;

    kaapi_thread_t* thief_thread = kaapi_request_getthread(request);
    kaapi_task_t* thief_task = kaapi_thread_toptask(thief_thread);

    /* allocate task stack */
    work_t* twork = (work_t*)
      (kaapi_thread_pushdata_align(thief_thread, sizeof(work_t), 8));

    /* allocate task result */
    kaapi_taskadaptive_result_t* const ktr =
      kaapi_allocate_thief_result(sc, sizeof(work_t), NULL);

    /* initialize ktr->data range in case of non preemption */
    ((work_t*)ktr->data)->range.i = 0;
    ((work_t*)ktr->data)->range.j = 0;

    /* initialize task stack */
    prepare_par_work
      (twork, vwork->a, vwork->ai, vwork->b, vwork->bj, sc, ktr);

    /* steal no more than unit_size */
    if (unit_size > (unsigned int)get_range_size(&stolen_range))
      unit_size = get_range_size(&stolen_range);
    steal_range(&twork->range, unit_size, &stolen_range);

    kaapi_task_init(thief_task, task_entry, twork);
    kaapi_thread_pushtask(thief_thread);
    kaapi_request_reply_head(sc, request, ktr);

    kaapi_set_workload(kaapi_request_kproc(request), unit_size);

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

  /* prepare seq result */
  matrix_elem_t res;
  matrix_elem_init(res);

  /* extract sequential work */
 redo_work:
  while (1)
  {
    int steal_res;

    lock_work(par_work);
    steal_res = next_seq_work(&seq_work, par_work);
    unlock_work(par_work);

    if (steal_res == -1)
      break ;

    /* inner product */
    for (size_t k = seq_work.range.i; k < seq_work.range.j; ++k)
    {
	/* e += a * b; */
	const matrix_elem_t* a = matrix_const_at(par_work->a, par_work->ai, k);
	const matrix_elem_t* b = matrix_const_at(par_work->b, k, par_work->bj);
	matrix_elem_addmul(res, *a, *b);
    }

    /* update result */
    matrix_elem_add(par_work->res, res);

    /* check for preemption */
#warning foobarbaz
#if 0
    if (par_work->ktr != NULL)
    {
      const int is_preempted = kaapi_preemptpoint
	(par_work->ktr, sc, NULL, NULL, par_work, sizeof(work_t), NULL);
      if (is_preempted)
	goto finalize_task;
    }
#endif
  }

  /* thief preemption */
  kaapi_taskadaptive_result_t* const ktr = kaapi_get_thief_head(sc);
  if (ktr != NULL)
  {
    kaapi_preempt_thief(sc, ktr, NULL, reduce_function, par_work);
    goto redo_work;
  }

 finalize_task:
  matrix_elem_clear(res);
  kaapi_set_self_workload(0);
  kaapi_steal_finalize(sc);
}

void innerprod
(
 matrix_elem_t res,
 const matrix_t* a,
 const size_t ai,
 const matrix_t* b,
 const size_t bj
)
{
  work_t par_work;
  kaapi_thread_t* thread;
  kaapi_task_t* task;
  kaapi_frame_t frame;

  prepare_par_work(&par_work, a, ai, b, bj, NULL, NULL);

  /* create and run main task */
  kaapi_set_self_workload(get_range_size(&par_work.range));
  thread = kaapi_self_thread();
  kaapi_thread_save_frame(thread, &frame);
  task = kaapi_thread_toptask(thread);
  kaapi_task_init(task, task_entry, &par_work);
  kaapi_thread_pushtask(thread);
  kaapi_sched_sync();
  kaapi_thread_restore_frame(thread, &frame);
  kaapi_set_self_workload(0);

  matrix_elem_set(res, par_work.res);
}

#if 0 /* sequential inner product */
void innerprod
(
 const matrix_t* a,
 const size_t ai,
 const matrix_t* b,
 const size_t bj
)
{
  matrix_elem_t e;

  matrix_elem_init(e);

  for (size_t k = 0; k < rhs->size1; ++k)
  {
    /* e += l * r; */
    const matrix_elem_t* const l = matrix_const_at(lhs, i, k);
    const matrix_elem_t* const r = matrix_const_at(rhs, k, j);
    matrix_elem_addmul(*e, *l, *r);
  }

  return e;
}
#endif
