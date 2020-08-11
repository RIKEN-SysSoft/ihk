#ifndef __ARR_UTILS_H_INCLUDED__
#define __ARR_UTILS_H_INCLUDED__


static inline void arr_sort(int arr[], int n)
{
  int i, j, min_idx;

  if (arr == NULL || n <= 0) return;

  // One by one move boundary of unsorted subarray
  for (i = 0; i < n - 1; i++) {
    // Find the minimum element in unsorted array
    min_idx = i;
    for (j = i + 1; j < n; j++)
      if (arr[j] < arr[min_idx])
        min_idx = j;

    // Swap the found minimum element
    // with the first element
    {
      int temp = arr[min_idx];
      arr[min_idx] = arr[i];
      arr[i] = temp;
    }
  }
}

static inline int arr_is_intersect(int arr1[], int n1, int arr2[], int n2)
{
  int i, j;
  if (!arr1 || !arr2 || n1 == 0 || n2 == 0)
    return 0;

  for (i = 0; i < n1; ++i) {
    for (j = 0; j < n2; ++j) {
      if (arr1[i] == arr2[j] ) {
        return 1;
      }
    }
  }
  return 0;
}

static inline int arr_equals(int arr1[], int arr2[], int n)
{
  if (!arr1 && !arr2) return 1;
  if (!arr1 && arr2) return 0;
  if (arr1 && !arr2) return 0;

  int i;
  for (i = 0; i < n; i++) {
    if (arr1[i] != arr2[i]) {
      return 0;
    }
  }
  return 1;
}

static inline int arr_has_unique_elements(int arr[], int n)
{
  if (!arr || n <= 0) return 0;

  arr_sort(arr, n);
  int i;
  for (i = 0; i < n-1; i++) {
    if (arr[i] == arr[i+1]) {
      return 0;
    }
  }
  return 1;
}

static inline int arr_first_diff_pos(int arr1[], int arr2[], int n_min)
{
  if (!arr1 || !arr2 || !n_min) return -1;
  int i;
  for (i = 0; i < n_min; i++) {
    if (arr1[i] != arr2[i]) return i;
  }
  return -1;
}

static inline void arr_copy_and_add(int dst[], int sorted_src[],
                                    int n_src, int ele)
{
  if (!dst || !sorted_src || !n_src) return;
  int i, j;
  for (i = 0; i < n_src; i++) {
    if (sorted_src[i] >= ele) {
      dst[i] = ele;
      break;
    }
    dst[i] = sorted_src[i];
  }
  if (i == n_src) {
    dst[i] = ele;
    return;
  }
  for (j = i+1; j <= n_src; j++) {
    dst[j] = sorted_src[j-1];
  }
}

#endif
