#ifndef __FS_USER_H_INCLUDED__
#define __FS_USER_H_INCLUDED__

#include <unistd.h>
#include <sys/stat.h>

static inline int fs_file_exist(const char *path)
{
  if (access(path, F_OK) == -1)
    return 0;
  return 1;
}

static inline int fs_folder_exist(const char *path)
{
  struct stat stats;
  stat(path, &stats);
  if (S_ISDIR(stats.st_mode))
    return 1;
  return 0;
}

static inline int fs_os_procfs_entry_exist(int os_index)
{
  char path[20] = "\0";
  sprintf(path, "/proc/mcos%d", os_index);
  return fs_folder_exist(path);
}

static inline int fs_os_sysfs_entry_exist(int os_index)
{
  char path[200] = "\0";
  sprintf(path, "/sys/devices/virtual/mcos/mcos%d/sys", os_index);
  return fs_folder_exist(path);
}

#endif
