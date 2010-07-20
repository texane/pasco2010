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


/* auto expanding line */

typedef struct line
{
  char* data;
  size_t len;
  unsigned int is_allocated;
} line_t;

static line_t line = { NULL, 0, 0 };

static void __attribute__((unused)) line_free(line_t* line)
{
  if (line->is_allocated)
    free(line->data);
}

static int line_realloc(line_t* line, size_t len)
{
  /* assume len > line->len */

  if (line->data == NULL)
  {
    static char line_buf[4096 * 4];

    line->data = line_buf;
    line->len = sizeof(line_buf);
    line->is_allocated = 0;
  }
  else if (line->is_allocated == 0)
  {
    char* const data = malloc(len);
    if (data == NULL)
      return -1;

    memcpy(data, line->data, line->len);
    line->data = data;
    line->len = len;
    line->is_allocated = 1;
  }
  else /* line->is_allocated */
  {
    line->data = realloc(line->data, len);
    if (line->data == NULL)
      return -1;
    line->len = len;
  }

  return 0;
}

static inline int line_read_lu_lu(line_t* line, size_t* a, size_t* b)
{
  /* todo: check return */
  sscanf(line->data, "%lu %lu", a, b);
  return 0;
}

static inline size_t line_write_lu_lu(line_t* line, size_t a, size_t b)
{
  if (line->len < 64)
    if (line_realloc(line, 64))
      return 0;

  return sprintf(line->data, "%lu %lu\n", a, b);
}


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

static int read_line(mapped_file_t* mf, line_t* line)
{
  const unsigned char* end = mf->base + mf->len;
  const unsigned char* const base = mf->base + mf->off;
  const unsigned char* p;
  size_t skipnl = 0;
  size_t len = 0;
  char* s;

  /* init the line once */
  if (line->data == NULL)
  {
    /* dont care about the size */
    if (line_realloc(line, 256) == -1)
      return -1;
  }

  for (p = base, s = line->data; p != end; ++p, ++s, ++len)
  {
    if (*p == '\n')
    {
      skipnl = 1;
      break;
    }

    /* has to reallocate */
    if (len == line->len)
    {
      if (line_realloc(line, len * 2))
	return -1;
      s = line->data + len;
    }

    *s = (char)*p;
  }

  /* zero terminated */
  if (len == line->len)
  {
    if (line_realloc(line, len + 1))
      return -1;
    s = line->data + len;
  }

  *s = 0;

  if (p == base)
    return -1;

  /* update offset */
  mf->off += (p - base) + skipnl;

  return 0;
}

static inline matrix_t* allocate_matrix(size_t size1, size_t size2)
{
  matrix_t* const m = malloc(sizeof(matrix_t));
  if (m == NULL)
    goto on_error;

  m->data = malloc(size1 * size2 * sizeof(matrix_elem_t));
  if (m->data == NULL)
    goto on_error;

  m->size1 = size1;
  m->size2 = size2;
  m->ld = size2;

  return m;

 on_error:
  if (m != NULL)
    free(m);
  return NULL;
}


/* exported */

int matrix_load_file(matrix_t** m, const char* path)
{
  mapped_file_t mf;
  int error = -1;
  size_t size1;
  size_t size2;
  size_t j = 0;
  size_t i = 0;

  *m = NULL;

  if (map_file(&mf, path) == -1)
    return -1;

  /* rows, lines */
  if (read_line(&mf, &line) == -1)
    goto on_error;

  if (line_read_lu_lu(&line, &size1, &size2))
    goto on_error;

  /* allocate m, n matrix */
  *m = allocate_matrix(size1, size2);
  if (*m == NULL)
    goto on_error;

  while ((read_line(&mf, &line)) != -1)
  {
    matrix_elem_t* const elem = matrix_at(*m, i, j);
    if (matrix_elem_set_str(*elem, line.data))
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

int matrix_store_file(const matrix_t* m, const char* path)
{
  const int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0755);
  if (fd == -1)
    return -1;

  size_t len;
  size_t i;
  size_t j;

  /* write dim */
  len = line_write_lu_lu(&line, m->size1, m->size2);
  write(fd, line.data, len);

  /* bigints */
  for (i = 0; i < m->size1; ++i)
  {
    for (j = 0; j < m->size2; ++j)
    {
      const matrix_elem_t* const elem = matrix_const_at(m, i, j);

      /* realloc if needed */
      len = matrix_elem_strlen(*elem);
      if (len >= line.len)
      {
	if (line_realloc(&line, len) == -1)
	  goto on_error;
      }

      matrix_elem_get_str(*elem, line.data);
      len = strlen(line.data);
      line.data[len++] = '\n';
      write(fd, line.data, len);
    }
  }

 on_error:
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
      printf("%s ", matrix_elem_get_str(*elem, line.data));
    }
    printf("\n");
  }
}

int matrix_create(matrix_t** m, size_t size1, size_t size2)
{
  *m = allocate_matrix(size1, size2);
  if (*m == NULL)
    return -1;

  return 0;
}

void matrix_destroy(matrix_t* m)
{
  size_t i, j;

  for (i = 0; i < m->size1; ++i)
  {
    for (j = 0; j < m->size2; ++j)
    {
      matrix_elem_t* const elem = matrix_at(m, i, j);
      matrix_elem_clear(*elem);
    }
  }

  free(m->data);
  free(m);
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
