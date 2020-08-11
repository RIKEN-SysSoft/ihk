#ifndef __BRANCH_INFO_H_INCLUDED__
#define __BRANCH_INFO_H_INCLUDED__

typedef struct branch_info {
  int expected;
  char *name;
} branch_info_t;

#define BRANCH_RET_CHK(ret, expected) \
  OKNG(ret == expected, "return value: %d, expected: %d\n", ret, expected);

#endif
