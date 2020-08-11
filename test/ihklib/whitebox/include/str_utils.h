#ifndef __STR_UTILS_H_INCLUDED__
#define __STR_UTILS_H_INCLUDED__

inline int str_end_with(char *str, char *suffix)
{
  if (!str || !suffix)
      return 0;
  size_t lenstr = strlen(str);
  size_t lensuffix = strlen(suffix);
  if (lensuffix >  lenstr)
      return 0;
  return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

#endif
