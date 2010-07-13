/*
** Made by fabien le mentec <texane@gmail.com>
** 
** Started on  Sat Jul 10 09:21:21 2010 texane
** Last update Mon Jul 12 04:04:00 2010 fabien le mentec
*/


#ifndef MATRIX_H_INCLUDED
# define MATRIX_H_INCLUDED


#include <sys/types.h>
#include "gmp.h"


/* matrix element wrappers */

typedef mpz_t matrix_elem_t;

static inline void matrix_elem_init(matrix_elem_t elem)
{
  /* set value to 0 */
  mpz_init(elem);
}

static inline void matrix_elem_clear(matrix_elem_t elem)
{
  mpz_clear(elem);
}

static inline int matrix_elem_set_str
(matrix_elem_t elem, const char* s)
{
  return mpz_init_set_str(elem, s, 10);
}

static inline char* matrix_elem_get_str
(const matrix_elem_t elem, char* s)
{
  return mpz_get_str(s, 10, elem);
}

static inline void matrix_elem_mul
(matrix_elem_t res, const matrix_elem_t lhs, const matrix_elem_t rhs)
{
  mpz_mul(res, lhs, rhs);
}

static inline void matrix_elem_add
(matrix_elem_t res, const matrix_elem_t op)
{
  /* res *= op */
  mpz_add(res, res, op);
}


static inline void matrix_elem_addmul
(matrix_elem_t res, const matrix_elem_t lhs, const matrix_elem_t rhs)
{
  /* res += lhs * rhs; */
  mpz_addmul(res, lhs, rhs);
}

static inline int matrix_elem_cmp
(const matrix_elem_t lhs, const matrix_elem_t rhs)
{
  return mpz_cmp(lhs, rhs);
}


/* matrix storage */

typedef struct matrix
{
#define MATRIX_FLAG_MPZ_ARRAY (1 << 0)
  unsigned int flags;

  size_t size1;
  size_t size2;

  matrix_elem_t data[1] __attribute__((aligned(64)));
} matrix_t;


/* exported non inlined */
int matrix_load_file(matrix_t**, const char*, size_t);
int matrix_load_file_transposed(matrix_t**, const char*, size_t);
int matrix_store_file(const matrix_t*, const char*);
void matrix_print(const matrix_t*);
int matrix_create(matrix_t**, size_t, size_t, size_t);
void matrix_gen_rand(matrix_t*);
void matrix_destroy(matrix_t*);
int matrix_cmp(const matrix_t*, const matrix_t*);

/* exported inlined */
static inline matrix_elem_t* matrix_at(matrix_t* m, size_t i, size_t j)
{
  return &m->data[i * m->size2 + j];
}

static inline const matrix_elem_t* matrix_const_at
(const matrix_t* m, size_t i, size_t j)
{
  return &m->data[i * m->size2 + j];
}

static inline matrix_elem_t* matrix_at_transposed
(matrix_t* m, size_t i, size_t j)
{
  return &m->data[j * m->size2 + i];
}

static inline const matrix_elem_t* matrix_const_at_transposed
(const matrix_t* m, size_t i, size_t j)
{
  return &m->data[j * m->size2 + i];
}


#endif /* ! MATRIX_H_INCLUDED */
