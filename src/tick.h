/*
** Made by fabien le mentec <texane@gmail.com>
** 
** Started on  Sat Jul 10 09:21:16 2010 texane
** Last update Sat Jul 10 09:31:10 2010 texane
*/


#ifndef TICK_H_INCLUDED
# define TICK_H_INCLUDED


#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>

extern int usleep(unsigned int);


union uint64_union
{
  uint64_t value;
  struct
  {
    uint32_t lo;
    uint32_t hi;
  } sub;
};

typedef union uint64_union uint64_union_t;
typedef uint64_union_t tick_counter_t;

static inline void tick_read(tick_counter_t* n)
{
  __asm__ __volatile__("rdtsc" : "=a" (n->sub.lo), "=d" (n->sub.hi));
}

static inline void tick_sub
(tick_counter_t* res, tick_counter_t* lhs, tick_counter_t* rhs)
{ 
  res->value = lhs->value - rhs->value;
}

static double scale_time = 0.0;

static void tick_init(void)
{
  tick_counter_t ticks[3];
  struct timeval tv1, tv2;
    
  tick_read(&ticks[0]);
  gettimeofday(&tv1, NULL);
  usleep(500000);
  tick_read(&ticks[1]);
  gettimeofday(&tv2, NULL);

  tick_sub(&ticks[2], &ticks[1], &ticks[0]);
  scale_time = ((tv2.tv_sec*1e6 + tv2.tv_usec) - (tv1.tv_sec*1e6 + tv1.tv_usec)) / (double)ticks[2].value;
}

static inline double tick_to_usec(const tick_counter_t* n)
{
  return (double)n->value * scale_time;
}


#endif /* ! TICK_H_INCLUDED */
