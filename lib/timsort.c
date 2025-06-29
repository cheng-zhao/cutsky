/*******************************************************************************
* timsort.c: this file is part of the cutsky program.

* cutsky: cutsky catalogue generator.

* Github repository:
        https://github.com/cheng-zhao/cutsky

* Copyright (c) 2023-2025 Cheng Zhao <zhaocheng03@gmail.com>  [MIT license]
 
*******************************************************************************/

#if defined(TIMSORT_DTYPE1) && defined(TIMSORT_DTYPE2) && \
  defined(TIMSORT_BIND_DTYPE) && defined(TIMSORT_OFFSET) && \
  defined(TIMSORT_CMP_IDX) && defined(TIMSORT_CMP_BIND) && \
  defined(TIMSORT_ASSIGN_IDX) && defined(TIMSORT_ASSIGN_BIND) && \
  defined(TIMSORT_GET_BIND) && defined(TIMSORT_SWAP)

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
  Implementation of the timsort algorithm for sorting two arrays, based on
  https://github.com/swenson/sort

  The original source codes are released under the MIT license, by
  Copyright (c) 2010-2019 Christopher Swenson and others as listed in
  https://github.com/swenson/sort/blob/master/CONTRIBUTORS.md

  The data types and rules for comparisons have to be supplied as macros.
*******************************************************************************/

#ifndef TIMSORT_ERR_MEMORY
#define TIMSORT_ERR_MEMORY      (-1)
#endif

/* Stack size for timsort. */
#define TIM_SORT_STACK_SIZE 128

/* Macro for conditional swap based on the indices. */
#define SORT_CSWAP(x,y,i,j) {                                   \
  if (TIMSORT_CMP_IDX(x,y,i,j) > 0) TIMSORT_SWAP(x,y,i,j);      \
}

/*============================================================================*\
                        Data structures used for timsort
\*============================================================================*/

/* Structure for storing information of runs. */
typedef struct {
  size_t start;
  size_t length;
} tim_sort_run_t;

/* Structure for saving temporary arrays. */
typedef struct {
  size_t alloc;
  TIMSORT_BIND_DTYPE *storage;
} temp_storage_t;


/*============================================================================*\
                 Functions for determining the minimum run size
\*============================================================================*/

/******************************************************************************
Function `clzll`:
  Count the leading zero bits of a 64-bit integer.
  Adapted from Hacker's Delight.
Arguments:
  * `x`:        the integer to be counted.
Return:
  Number of leading zero bits.
******************************************************************************/
static inline int clzll(uint64_t x) {
  if (x == 0) return 64;
  int n = 0;
  if (x <= 0x00000000FFFFFFFFL) {
    n = n + 32;
    x = x << 32;
  }
  if (x <= 0x0000FFFFFFFFFFFFL) {
    n = n + 16;
    x = x << 16;
  }
  if (x <= 0x00FFFFFFFFFFFFFFL) {
    n = n + 8;
    x = x << 8;
  }
  if (x <= 0x0FFFFFFFFFFFFFFFL) {
    n = n + 4;
    x = x << 4;
  }
  if (x <= 0x3FFFFFFFFFFFFFFFL) {
    n = n + 2;
    x = x << 2;
  }
  if (x <= 0x7FFFFFFFFFFFFFFFL) {
    n = n + 1;
  }
  return n;
}

/******************************************************************************
Function `compute_minrun`:
  Compute the minimum run size for efficient merging.
Arguments:
  * `size`:     size of the array to be split.
Return:
  The appropriate number of runs.
******************************************************************************/
static inline int compute_minrun(const uint64_t size) {
  const int top_bit = 64 - clzll(size);
  const int shift = (top_bit > 6) ? top_bit - 6 : 0;
  const int minrun = (int) (size >> shift);
  const uint64_t mask = (1ULL << shift) - 1;
  if (mask & size) return minrun + 1;
  return minrun;
}


/*============================================================================*\
                      Functions for binary insertion sort
\*============================================================================*/

/******************************************************************************
Function `binary_insertion_find`:
  Perform binary search for binary insertion sort.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `v`:        the binded variable to be compared with the arrays;
  * `size`:     size of the array.
Return:
  Index of the value to be inserted.
******************************************************************************/
static inline size_t binary_insertion_find(TIMSORT_DTYPE1 *x,
    TIMSORT_DTYPE2 *y, const TIMSORT_BIND_DTYPE v, const size_t size) {
  size_t r = size - 1;
  /* Check for out of bounds at the beginning. */
  if (TIMSORT_CMP_BIND(x, y, 0, v) > 0) return 0;
  else if (TIMSORT_CMP_BIND(x, y, r, v) < 0) return r;

  size_t l = 0;
  for (size_t c = r >> 1; ; c = l + ((r - l) >> 1)) {
    if (TIMSORT_CMP_BIND(x, y, c, v) > 0) {
      if (c - l <= 1) return c;
      r = c;
    }
    /* Allow = for stability. Binary search favors the right. */
    else {      
      if (r - c <= 1) return c + 1;
      l = c;
    }
  }
}

/******************************************************************************
Function `binary_insertion_sort_start`:
  Sort two arrays based on the first one, using binary insertion sort, but
  knowing that the first `start` entries are sorted.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `offset`:   starting index of the two arrays to be considered;
  * `start`:    number of leading elements that are sorted already;
  * `size`:     size of the arrays.
******************************************************************************/
static inline void binary_insertion_sort_start(TIMSORT_DTYPE1 *x,
    TIMSORT_DTYPE2 *y, const size_t offset, const size_t start,
    const size_t size) {
  TIMSORT_OFFSET(x, y, offset);
  for (size_t i = start; i < size; i++) {
    /* If this entry is already correct, just move along */
    if (TIMSORT_CMP_IDX(x, y, i - 1, i) <= 0) continue;

    /* Find the right place, shift everything over, and squeeze in. */
    TIMSORT_BIND_DTYPE v;
    TIMSORT_ASSIGN_BIND(x, y, i, v);
    size_t location = binary_insertion_find(x, y, v, i);
    for (size_t j = i - 1; j >= location; j--) {
      TIMSORT_ASSIGN_IDX(x, y, j, j + 1);
      if (j == 0) break;        /* check edge case because j is unsigned */
    }
    TIMSORT_GET_BIND(x, y, location, v);
  }
}

/******************************************************************************
Function `binary_insertion_sort`:
  Sort two arrays based on the first one, using binary insertion sort.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `size`:     size of the arrays.
******************************************************************************/
static void binary_insertion_sort(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y,
    const size_t size) {
  if (size <= 1) return;
  binary_insertion_sort_start(x, y, 0, 1, size);
}


/*============================================================================*\
                           Functions for bitonic sort
\*============================================================================*/

/* The full implementation of a bitonic sort is not here. Since we only want to
   use sorting networks for small length lists we create optimal sorting
   networks for lists of length <= 16 and call out to binary_insertion_sort
   for anything larger than 16.
   Optimal sorting networks for small length lists.
   Taken from https://pages.ripco.net/~jgamble/nw.html */

/******************************************************************************
Function `bitonic_sort_X` (X = 2,3,4,...,16):
  Sort two arrays with given lengths based on the first one, using bitonic sort.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted.
******************************************************************************/
static inline void bitonic_sort_2(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
}

static inline void bitonic_sort_3(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 0, 1);
}

static inline void bitonic_sort_4(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 1, 2);
}

static inline void bitonic_sort_5(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 0, 3);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 1, 2);
}

static inline void bitonic_sort_6(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 2, 5);
  SORT_CSWAP(x, y, 0, 3);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 2, 3);
}

static inline void bitonic_sort_7(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 0, 3);
  SORT_CSWAP(x, y, 2, 5);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 2, 3);
}

static inline void bitonic_sort_8(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 3, 7);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 3, 6);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 3, 4);
}

static inline void bitonic_sort_9(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 7, 8);
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 2, 5);
  SORT_CSWAP(x, y, 0, 3);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 5, 8);
  SORT_CSWAP(x, y, 3, 6);
  SORT_CSWAP(x, y, 4, 7);
  SORT_CSWAP(x, y, 2, 5);
  SORT_CSWAP(x, y, 0, 3);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 2, 3);
}

static inline void bitonic_sort_10(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 4, 9);
  SORT_CSWAP(x, y, 3, 8);
  SORT_CSWAP(x, y, 2, 7);
  SORT_CSWAP(x, y, 1, 6);
  SORT_CSWAP(x, y, 0, 5);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 6, 9);
  SORT_CSWAP(x, y, 0, 3);
  SORT_CSWAP(x, y, 5, 8);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 3, 6);
  SORT_CSWAP(x, y, 7, 9);
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 7, 8);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 2, 5);
  SORT_CSWAP(x, y, 6, 8);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 4, 7);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 4, 5);
}

static inline void bitonic_sort_11(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 8, 10);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 3, 7);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 6, 10);
  SORT_CSWAP(x, y, 4, 8);
  SORT_CSWAP(x, y, 5, 9);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 3, 8);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 6, 10);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 7, 10);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 6, 8);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 7, 9);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 7, 8);
}

static inline void bitonic_sort_12(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 10, 11);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 9, 11);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 8, 10);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 7, 11);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 6, 10);
  SORT_CSWAP(x, y, 3, 7);
  SORT_CSWAP(x, y, 4, 8);
  SORT_CSWAP(x, y, 5, 9);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 7, 11);
  SORT_CSWAP(x, y, 3, 8);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 6, 10);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 7, 10);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 6, 8);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 7, 9);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 7, 8);
}

static inline void bitonic_sort_13(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 1, 7);
  SORT_CSWAP(x, y, 9, 11);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 8);
  SORT_CSWAP(x, y, 0, 12);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 8, 11);
  SORT_CSWAP(x, y, 7, 12);
  SORT_CSWAP(x, y, 5, 9);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 3, 7);
  SORT_CSWAP(x, y, 10, 11);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 6, 12);
  SORT_CSWAP(x, y, 7, 8);
  SORT_CSWAP(x, y, 11, 12);
  SORT_CSWAP(x, y, 4, 9);
  SORT_CSWAP(x, y, 6, 10);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 10, 11);
  SORT_CSWAP(x, y, 1, 7);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 9, 11);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 4, 7);
  SORT_CSWAP(x, y, 8, 10);
  SORT_CSWAP(x, y, 0, 5);
  SORT_CSWAP(x, y, 2, 5);
  SORT_CSWAP(x, y, 6, 8);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 7, 8);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 6);
}

static inline void bitonic_sort_14(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 10, 11);
  SORT_CSWAP(x, y, 12, 13);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 8, 10);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 9, 11);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 8, 12);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 9, 13);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 3, 7);
  SORT_CSWAP(x, y, 0, 8);
  SORT_CSWAP(x, y, 1, 9);
  SORT_CSWAP(x, y, 2, 10);
  SORT_CSWAP(x, y, 3, 11);
  SORT_CSWAP(x, y, 4, 12);
  SORT_CSWAP(x, y, 5, 13);
  SORT_CSWAP(x, y, 5, 10);
  SORT_CSWAP(x, y, 6, 9);
  SORT_CSWAP(x, y, 3, 12);
  SORT_CSWAP(x, y, 7, 11);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 4, 8);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 7, 13);
  SORT_CSWAP(x, y, 2, 8);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 11, 13);
  SORT_CSWAP(x, y, 3, 8);
  SORT_CSWAP(x, y, 7, 12);
  SORT_CSWAP(x, y, 6, 8);
  SORT_CSWAP(x, y, 10, 12);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 7, 9);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 7, 8);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 11, 12);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
}

static inline void bitonic_sort_15(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 10, 11);
  SORT_CSWAP(x, y, 12, 13);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 8, 10);
  SORT_CSWAP(x, y, 12, 14);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 9, 11);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 8, 12);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 9, 13);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 10, 14);
  SORT_CSWAP(x, y, 3, 7);
  SORT_CSWAP(x, y, 0, 8);
  SORT_CSWAP(x, y, 1, 9);
  SORT_CSWAP(x, y, 2, 10);
  SORT_CSWAP(x, y, 3, 11);
  SORT_CSWAP(x, y, 4, 12);
  SORT_CSWAP(x, y, 5, 13);
  SORT_CSWAP(x, y, 6, 14);
  SORT_CSWAP(x, y, 5, 10);
  SORT_CSWAP(x, y, 6, 9);
  SORT_CSWAP(x, y, 3, 12);
  SORT_CSWAP(x, y, 13, 14);
  SORT_CSWAP(x, y, 7, 11);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 4, 8);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 7, 13);
  SORT_CSWAP(x, y, 2, 8);
  SORT_CSWAP(x, y, 11, 14);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 11, 13);
  SORT_CSWAP(x, y, 3, 8);
  SORT_CSWAP(x, y, 7, 12);
  SORT_CSWAP(x, y, 6, 8);
  SORT_CSWAP(x, y, 10, 12);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 7, 9);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 7, 8);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 11, 12);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
}

static inline void bitonic_sort_16(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y) {
  SORT_CSWAP(x, y, 0, 1);
  SORT_CSWAP(x, y, 2, 3);
  SORT_CSWAP(x, y, 4, 5);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
  SORT_CSWAP(x, y, 10, 11);
  SORT_CSWAP(x, y, 12, 13);
  SORT_CSWAP(x, y, 14, 15);
  SORT_CSWAP(x, y, 0, 2);
  SORT_CSWAP(x, y, 4, 6);
  SORT_CSWAP(x, y, 8, 10);
  SORT_CSWAP(x, y, 12, 14);
  SORT_CSWAP(x, y, 1, 3);
  SORT_CSWAP(x, y, 5, 7);
  SORT_CSWAP(x, y, 9, 11);
  SORT_CSWAP(x, y, 13, 15);
  SORT_CSWAP(x, y, 0, 4);
  SORT_CSWAP(x, y, 8, 12);
  SORT_CSWAP(x, y, 1, 5);
  SORT_CSWAP(x, y, 9, 13);
  SORT_CSWAP(x, y, 2, 6);
  SORT_CSWAP(x, y, 10, 14);
  SORT_CSWAP(x, y, 3, 7);
  SORT_CSWAP(x, y, 11, 15);
  SORT_CSWAP(x, y, 0, 8);
  SORT_CSWAP(x, y, 1, 9);
  SORT_CSWAP(x, y, 2, 10);
  SORT_CSWAP(x, y, 3, 11);
  SORT_CSWAP(x, y, 4, 12);
  SORT_CSWAP(x, y, 5, 13);
  SORT_CSWAP(x, y, 6, 14);
  SORT_CSWAP(x, y, 7, 15);
  SORT_CSWAP(x, y, 5, 10);
  SORT_CSWAP(x, y, 6, 9);
  SORT_CSWAP(x, y, 3, 12);
  SORT_CSWAP(x, y, 13, 14);
  SORT_CSWAP(x, y, 7, 11);
  SORT_CSWAP(x, y, 1, 2);
  SORT_CSWAP(x, y, 4, 8);
  SORT_CSWAP(x, y, 1, 4);
  SORT_CSWAP(x, y, 7, 13);
  SORT_CSWAP(x, y, 2, 8);
  SORT_CSWAP(x, y, 11, 14);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 2, 4);
  SORT_CSWAP(x, y, 11, 13);
  SORT_CSWAP(x, y, 3, 8);
  SORT_CSWAP(x, y, 7, 12);
  SORT_CSWAP(x, y, 6, 8);
  SORT_CSWAP(x, y, 10, 12);
  SORT_CSWAP(x, y, 3, 5);
  SORT_CSWAP(x, y, 7, 9);
  SORT_CSWAP(x, y, 3, 4);
  SORT_CSWAP(x, y, 5, 6);
  SORT_CSWAP(x, y, 7, 8);
  SORT_CSWAP(x, y, 9, 10);
  SORT_CSWAP(x, y, 11, 12);
  SORT_CSWAP(x, y, 6, 7);
  SORT_CSWAP(x, y, 8, 9);
}

/******************************************************************************
Function `bitonic_sort`:
  Sort two arrays based on the first one, using bitonic sort and
  binary insertion sort, depending on the lengths of the arrays.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `size`:     size of the arrays.
******************************************************************************/
static void bitonic_sort(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y,
    const size_t size) {
  switch (size) {
    case 0:
    case 1:
      break;
    case 2:
      bitonic_sort_2(x, y); break;
    case 3:
      bitonic_sort_3(x, y); break;
    case 4:
      bitonic_sort_4(x, y); break;
    case 5:
      bitonic_sort_5(x, y); break;
    case 6:
      bitonic_sort_6(x, y); break;
    case 7:
      bitonic_sort_7(x, y); break;
    case 8:
      bitonic_sort_8(x, y); break;
    case 9:
      bitonic_sort_9(x, y); break;
    case 10:
      bitonic_sort_10(x, y); break;
    case 11:
      bitonic_sort_11(x, y); break;
    case 12:
      bitonic_sort_12(x, y); break;
    case 13:
      bitonic_sort_13(x, y); break;
    case 14:
      bitonic_sort_14(x, y); break;
    case 15:
      bitonic_sort_15(x, y); break;
    case 16:
      bitonic_sort_16(x, y); break;
    default:
      binary_insertion_sort(x, y, size);
  }
}


/*============================================================================*\
                             Functions for timsort
\*============================================================================*/

/* The timsort implementation is based on timsort.txt:
   https://bugs.python.org/file4451/ */

/******************************************************************************
Function `reverse_elements`:
  Reverse elements of two arrays inside a given range of indices.
Arguments:
  * `x`:        the first array to be reversed;
  * `y`:        the second array to be reversed;
  * `start`:    starting index of the elements to be reversed;
  * `end`:      ending index of the elements to be reversed.
******************************************************************************/
static inline void reverse_elements(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y,
    size_t start, size_t end) {
  while (start < end) {
    TIMSORT_SWAP(x, y, start, end);
    start += 1;
    end -= 1;
  }
}

/******************************************************************************
Function `count_run`:
  Count the number of elements for the next run.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `start`:    starting index of the elements to be consulted;
  * `size`:     size of the two arrays.
Return:
  Length of already sorted blocks (either non-decreasing or descending).
******************************************************************************/
static size_t count_run(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y,
    const size_t start, const size_t size) {
  if (size - start == 1) return 1;
  if (start >= size - 2) {
    SORT_CSWAP(x, y, size - 2, size - 1);
    return 2;
  }

  size_t curr = start + 2;
  if (TIMSORT_CMP_IDX(x, y, start, start + 1) <= 0) {
    /* Increasing run. */
    for (; curr != size - 1; curr++) {
      if (TIMSORT_CMP_IDX(x, y, curr - 1, curr) > 0) break;
    }
    return curr - start;
  }
  else {
    /* Decreasing run. */
    for (; curr != size - 1; curr++) {
      if (TIMSORT_CMP_IDX(x, y, curr - 1, curr) <= 0) break;
    }
    /* Reverse in-place. */
    reverse_elements(x, y, start, curr - 1);
    return curr - start;
  }
}

/******************************************************************************
Function `check_invariant`:
  Check two invariants for the merge strategy.
Arguments:
  * `stack`:    stack storing runs to be merged;
  * `num`:      number of runs on the stack.
Return:
  True if the invariants are satisfied.
******************************************************************************/
static bool check_invariant(tim_sort_run_t *stack, const int num) {
  if (num < 2) return 1;
  if (num == 2)
    return (stack[num - 2].length > stack[num - 1].length) ? true : false;

  const size_t A = stack[num - 3].length;
  const size_t B = stack[num - 2].length;
  const size_t C = stack[num - 1].length;

  if ((A > B + C) && B > C) return true;
  return false;
}

/******************************************************************************
Function `tim_sort_resize`:
  Enlarge the memory allocated for storing runs when necessary.
Arguments:
  * `store`:    structure for storing temporary arrays;
  * `size`:     size of array to be stored temporarily.
******************************************************************************/
static void tim_sort_resize(temp_storage_t *store, const size_t size) {
  if ((store->storage == NULL) || (store->alloc < size)) {
    TIMSORT_BIND_DTYPE *tmp = realloc(store->storage,
        size * sizeof(TIMSORT_BIND_DTYPE));
    if (!tmp) {
      fprintf(stderr, "failed to allocate memory for tim sort\n");
      exit(TIMSORT_ERR_MEMORY);
    }
    store->storage = tmp;
    store->alloc = size;
  }
}

/******************************************************************************
Function `tim_sort_merge`:
  Merging adjacent runs.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `stack`:    stack for storing runs;
  * `nstack`:   size of the stack for storing runs;
  * `store`:    stucture for storing temporary arrays.
******************************************************************************/
static void tim_sort_merge(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y,
    const tim_sort_run_t *stack, const int nstack, temp_storage_t *store) {
  const size_t A = stack[nstack - 2].length;
  const size_t B = stack[nstack - 1].length;
  const size_t curr = stack[nstack - 2].start;
  tim_sort_resize(store, (A < B) ? A : B);
  TIMSORT_BIND_DTYPE *storage = store->storage;

  if (A < B) {          /* left merge */
    /* Copy A to the temp array. */
    for (size_t i = 0; i < A; i++) {
      TIMSORT_ASSIGN_BIND(x, y, curr + i, storage[i]);
    }

    size_t i = 0;
    size_t j = curr + A;
    for (size_t k = curr; k < curr + A + B; k++) {
      if ((i < A) && (j < curr + A + B)) {
        if (TIMSORT_CMP_BIND(x, y, j, storage[i]) >= 0) {
          TIMSORT_GET_BIND(x, y, k, storage[i]);
          i++;
        }
        else {
          TIMSORT_ASSIGN_IDX(x, y, j, k);
          j++;
        }
      }
      else if (i < A) {
        TIMSORT_GET_BIND(x, y, k, storage[i]);
        i++;
      }
      else break;
    }
  }
  else {                /* right merge */
    /* Copy B to the temp array. */
    for (size_t i = 0; i < B; i++) {
      TIMSORT_ASSIGN_BIND(x, y, curr + A + i, storage[i]);
    }

    size_t i = B;
    size_t j = curr + A;
    size_t k = curr + A + B;
    while (k-- > curr) {
      if ((i > 0) && (j > curr)) {
        if (TIMSORT_CMP_BIND(x, y, j - 1, storage[i - 1]) > 0) {
          --j;
          TIMSORT_ASSIGN_IDX(x, y, j, k);
        }
        else {
          --i;
          TIMSORT_GET_BIND(x, y, k, storage[i]);
        }
      }
      else if (i > 0) {
        --i;
        TIMSORT_GET_BIND(x, y, k, storage[i]);
      }
      else break;
    }
  }
}

/******************************************************************************
Function `tim_sort_collapse`:
  Merge a newly identified run with preceding ones if desired.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `stack`:    stack for storing runs;
  * `nstack`:   size of the stack for storing runs;
  * `store`:    stucture for storing temporary arrays;
  * `size`:     size of the arrays.
Return:
  Number of runs after merging.
******************************************************************************/
static int tim_sort_collapse(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y,
    tim_sort_run_t *stack, int nstack, temp_storage_t *store,
    const size_t size) {
  /* If the stack only has one thing on it, we are done with the collapse. */
  while (nstack > 1) {
    /* If this is the last merge, just do it. */
    if ((nstack == 2) && (stack[0].length + stack[1].length == size)) {
      tim_sort_merge(x, y, stack, nstack, store);
      stack[0].length += stack[1].length;
      nstack -= 1;
      break;
    }
    /* Check if the invariant is off for a stack of 2 elements. */
    else if ((nstack == 2) && (stack[0].length <= stack[1].length)) {
      tim_sort_merge(x, y, stack, nstack, store);
      stack[0].length += stack[1].length;
      nstack -= 1;
      break;
    }
    else if (nstack == 2) break;

    int ABC = 0;
    size_t B = stack[nstack - 3].length;
    size_t C = stack[nstack - 2].length;
    size_t D = stack[nstack - 1].length;
    if (nstack >= 4) {
      size_t A = stack[nstack - 4].length;
      ABC = (A <= B + C);
    }
    int BCD = (B <= C + D) || ABC;
    int CD = (C <= D);

    /* Both invariants are good. */
    if (!BCD && !CD) break;

    if (BCD && !CD) {           /* left merge */
      tim_sort_merge(x, y, stack, nstack - 1, store);
      stack[nstack - 3].length += stack[nstack - 2].length;
      stack[nstack - 2] = stack[nstack - 1];
      nstack -= 1;
    }
    else {                      /* right merge */
      tim_sort_merge(x, y, stack, nstack, store);
      stack[nstack - 2].length += stack[nstack - 1].length;
      nstack -= 1;
    }
  }
  return nstack;
}

/******************************************************************************
Function `push_next`:
  Push a run to the stack.
Arguments:
  * `x`:        the first array to be sorted;
  * `y`:        the second array to be sorted;
  * `size`:     size of the arrays;
  * `store`:    stucture for storing temporary arrays;
  * `minrun`:   minimum size of a run;
  * `stack`:    stack for storing runs;
  * `nstack`:   size of the stack for storing runs;
  * `curr`:     index of the current element.
Return:
  Zero if finished sorting; non-zero otherwise.
******************************************************************************/
static inline int push_next(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y,
    const size_t size, temp_storage_t *store, const size_t minrun,
    tim_sort_run_t *stack, size_t *nstack, size_t *curr) {
  size_t len = count_run(x, y, *curr, size);
  size_t run = minrun;

  if (run > size - *curr) run = size - *curr;
  if (run > len) {
    binary_insertion_sort_start(x, y, *curr, len, run);
    len = run;
  }

  stack[*nstack].start = *curr;
  stack[*nstack].length = len;
  *nstack += 1;
  *curr += len;

  /* Finish up. */
  if (*curr == size) {
    while (*nstack > 1) {
      tim_sort_merge(x, y, stack, (int) *nstack, store);
      stack[*nstack - 2].length += stack[*nstack - 1].length;
      *nstack -= 1;
    }

    if (store->storage != NULL) {
      free(store->storage);
      store->storage = NULL;
    }
    return 0;
  }

  return 1;
}


/*============================================================================*\
                              Interface of timsort
\*============================================================================*/

/******************************************************************************
Function `tim_sort`:
  Sort two arrays with predefined data types and comparison rules,
  using the timsort algorithm.
Arguments:
  * `x`:        pointer to the first array;
  * `y`:        pointer to the second array;
  * `size`:     size of the input arrays.
******************************************************************************/
static void tim_sort(TIMSORT_DTYPE1 *x, TIMSORT_DTYPE2 *y, const size_t size) {
  if (size <= 1) return;
  if (size < 64) {
    bitonic_sort(x, y, size);
    return;
  }

  /* Compute the minimum run length. */
  size_t minrun = compute_minrun(size);
  /* temporary storage for merges */
  temp_storage_t store;
  store.alloc = 0;
  store.storage = NULL;

  tim_sort_run_t run_stack[TIM_SORT_STACK_SIZE];
  size_t nstack = 0;
  size_t curr = 0;

  if (!push_next(x, y, size, &store, minrun, run_stack, &nstack, &curr)) return;
  if (!push_next(x, y, size, &store, minrun, run_stack, &nstack, &curr)) return;
  if (!push_next(x, y, size, &store, minrun, run_stack, &nstack, &curr)) return;

  for (;;) {
    if (!check_invariant(run_stack, (int) nstack)) {
      nstack = tim_sort_collapse(x, y, run_stack, (int) nstack, &store, size);
      continue;
    }
    if (!push_next(x, y, size, &store, minrun, run_stack, &nstack, &curr))
      return;
  }
}

#undef TIMSORT_DTYPE1
#undef TIMSORT_DTYPE2
#undef TIMSORT_BIND_DTYPE
#undef TIMSORT_OFFSET
#undef TIMSORT_CMP_IDX
#undef TIMSORT_CMP_BIND
#undef TIMSORT_ASSIGN_IDX
#undef TIMSORT_ASSIGN_BIND
#undef TIMSORT_GET_BIND
#undef TIMSORT_SWAP

#endif
