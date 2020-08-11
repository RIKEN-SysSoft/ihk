#include <linux/fs.h>

static inline int fs_file_exist(const char *path)
{
  struct file* filp = filp_open(path, O_RDONLY, 0);
  if (IS_ERR(filp)) return 0;
  filp_close(filp, NULL);
  return 1;
}

static inline int fs_folder_exist(const char *path)
{
  struct file* filp = filp_open(path, O_RDONLY | O_DIRECTORY, 0);
  if (IS_ERR(filp)) return 0;
  filp_close(filp, NULL);
  return 1;
}
