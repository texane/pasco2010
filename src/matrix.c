/*
** Made by fabien le mentec <texane@gmail.com>
** 
** Started on  Sat Jul 10 09:21:21 2010 texane
** Last update Mon Jul 12 04:04:00 2010 fabien le mentec
*/


#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "matrix.h"


/* global line buffer */
static char line_buf[4096 * 4];


/* memory mapped file */

typedef struct mapped_file
{
  unsigned char* base;
  size_t off;
  size_t len;
} mapped_file_t;

static int map_file(mapped_file_t* mf, const char* path)
{
  int error = -1;
  struct stat st;

  const int fd = open(path, O_RDONLY);
  if (fd == -1)
    return -1;

  if (fstat(fd, &st) == -1)
    goto on_error;

  mf->base = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (mf->base == MAP_FAILED)
    goto on_error;

  mf->off = 0;
  mf->len = st.st_size;

  /* success */
  error = 0;

 on_error:
  close(fd);

  return error;
}

static void unmap_file(mapped_file_t* mf)
{
  munmap(mf->base, mf->len);
  mf->base = MAP_FAILED;
  mf->len = 0;
}

static int read_line(mapped_file_t* mf, char** line)
{
  const unsigned char* end = mf->base + mf->len;
  const unsigned char* const base = mf->base + mf->off;
  const unsigned char* p;
  size_t skipnl = 0;
  char* s;

  *line = line_buf;

  for (p = base, s = line_buf; p != end; ++p, ++s)
  {
    if (*p == '\n')
    {
      skipnl = 1;
      break;
    }

    *s = (char)*p;
  }

  *s = 0;

  if (p == base)
    return -1;

  /* update offset */
  mf->off += (p - base) + skipnl;

  return 0;
}

static matrix_t* alloc_matrix(size_t size1, size_t size2, size_t size3)
{
  const size_t data_count = size1 * size2;
  const size_t data_size = (data_count * sizeof(matrix_elem_t));
  const size_t total_size = offsetof(matrix_t, data) + data_size;

  matrix_t* const m = malloc(total_size);
  if (m == NULL)
    return NULL;

  m->flags = 0;
  m->size1 = size1;
  m->size2 = size2;

  if (size3 == 0)
  {
    size_t i, j;
    for (i = 0; i < m->size1; ++i)
      for (j = 0; j < m->size2; ++j)
	mpz_init(*matrix_at(m, i, j));
  }
  else
  {
    m->flags |= MATRIX_FLAG_MPZ_ARRAY;
    mpz_array_init(m->data[0], data_count, size3);
  }

  return m;
}


static void free_matrix(matrix_t* m)
{
  if (m->flags & MATRIX_FLAG_MPZ_ARRAY)
  {
    mpz_clear(m->data[0]);
  }
  else
  {
    size_t i, j;
    for (i = 0; i < m->size1; ++i)
      for (j = 0; j < m->size2; ++j)
	mpz_clear(*matrix_at(m, i, j));
  }
  
  free(m);
}


/* load a matrix from file */

static int load_file
(matrix_t** m, const char* path, unsigned int is_transposed, size_t size3)
{
  mapped_file_t mf;
  int error = -1;
  size_t size1;
  size_t size2;
  size_t j = 0;
  size_t i = 0;
  char* line = NULL;

  *m = NULL;

  if (map_file(&mf, path) == -1)
    return -1;

  /* rows, lines */
  if (read_line(&mf, &line) == -1)
    goto on_error;

  /* todo: check return */
  sscanf(line, "%lu %lu", &size1, &size2);

  /* allocate m, n matrix */
  *m = alloc_matrix(size1, size2, size3);
  if (*m == NULL)
    goto on_error;

  while ((read_line(&mf, &line)) != -1)
  {
    matrix_elem_t* elem;

    if (is_transposed)
      elem = matrix_at(*m, j, i);
    else
      elem = matrix_at(*m, i, j);

    if (matrix_elem_set_str(*elem, line))
      goto on_error;

    /* next i, j */
    if ((++j) == size2)
    {
      j = 0;
      ++i;
    }
  }

  /* success */
  error = 0;

 on_error:
  unmap_file(&mf);

  return error;
}


/* exported */

int matrix_load_file(matrix_t** m, const char* path, size_t size3)
{
  return load_file(m, path, 0, size3);
}


int matrix_load_file_transposed(matrix_t** m, const char* path, size_t size3)
{
  return load_file(m, path, 1, size3);
}


int matrix_store_file(const matrix_t* m, const char* path)
{
  const int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0755);
  if (fd == -1)
    return -1;

  size_t len;
  size_t i;
  size_t j;

  /* write dim */
  len = sprintf(line_buf, "%lu %lu\n", m->size1, m->size2);
  write(fd, line_buf, len);

  /* bigints */
  for (i = 0; i < m->size1; ++i)
  {
    for (j = 0; j < m->size2; ++j)
    {
      const matrix_elem_t* const elem = matrix_const_at(m, i, j);
      matrix_elem_get_str(*elem, line_buf);
      len = strlen(line_buf);
      line_buf[len++] = '\n';
      write(fd, line_buf, len);
    }
  }

  close(fd);

  return 0;
}

void matrix_print(const matrix_t* m)
{
  size_t i;
  size_t j;

  for (i = 0; i < m->size1; ++i)
  {
    for (j = 0; j < m->size2; ++j)
    {
      const matrix_elem_t* const elem = matrix_const_at(m, i, j);
      printf("%s ", matrix_elem_get_str(*elem, line_buf));
    }
    printf("\n");
  }
}

int matrix_create(matrix_t** m, size_t size1, size_t size2)
{
  *m = alloc_matrix(size1, size2, 0);
  if (*m == NULL)
    return -1;

  return 0;
}

void matrix_gen_rand(matrix_t* m)
{
  size_t i, j;

  for (i = 0; i < m->size1; ++i)
  {
    for (j = 0; j < m->size2; ++j)
    {
      matrix_elem_t* const elem = matrix_at(m, i, j);
      sprintf(line_buf, "%u", (unsigned int)(rand() % 10));
      matrix_elem_set_str(*elem, line_buf);
    }
  }
}

void matrix_destroy(matrix_t* m)
{
  free_matrix(m);
}

int matrix_cmp(const matrix_t* lhs, const matrix_t* rhs)
{
  /* assume equal dims */

  size_t i;
  size_t j;

  for (i = 0; i < lhs->size1; ++i)
  {
    for (j = 0; j < lhs->size2; ++j)
    {
      const matrix_elem_t* const a = matrix_const_at(lhs, i, j);
      const matrix_elem_t* const b = matrix_const_at(rhs, i, j);

      if (matrix_elem_cmp(*a, *b))
	return -1;
    }
  }

  return 0;
}


/* unit testing */

#if CONFIG_MATRIX_UNIT

int main(int ac, char** av)
{
  matrix_t* a;

  if (matrix_load_file(&a, av[1]))
  {
    printf("load_file == -1\n");
    return -1;
  }
    
  matrix_store_file(a, av[2]);

  matrix_destroy(a);

  return 0;
}

#endif

/* unit testing */
