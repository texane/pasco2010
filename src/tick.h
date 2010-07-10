/*
** Made by fabien le mentec <texane@gmail.com>
** 
** Started on  Sat Jul 10 09:21:16 2010 texane
** Last update Sat Jul 10 09:31:10 2010 texane
*/


#ifndef TICK_H_INCLUDED
# define TICK_H_INCLUDED


#include <stdint.h>


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


#endif /* ! TICK_H_INCLUDED */
