#ifndef __FS_UTILS_H_INCLUDED__
#define __FS_UTILS_H_INCLUDED__

#include <linux/fs.h>

#define FS_MAX_PATH 200

static int fs_split_path(const char* _path, char* _parent, char* _child)
{
  int i = 0;
  int len = strlen(_path);
  if (_path[len - 1] != '/') {
    for (i = len - 1; i >= 0; i--) {
      if (_path[i] == '/') {
        if (i == 0) {
          sprintf(_parent, "%s", _path);
          _child[0] = '\0';
          return 0;
        }
        memcpy(_child, &_path[i + 1], len - 1 - i);
        memcpy(_parent, _path, i);
        _parent[i + 1] = '\0';
        _child[len - i - 1] = '\0';
        return 0;
      }
    }
  } else {
    char path_trim[FS_MAX_PATH] = "\0";
    sprintf(path_trim, "%s", _path);
    path_trim[len - 1] = '\0';
    return fs_split_path(path_trim, _parent, _child);
  }
  return 0;
}

struct dir_data {
	struct dir_context ctx;
  char* input_name;
	int result;
};

static inline int _fs_find_entry(
    struct dir_context *ctx, const char *name, int namlen,
    loff_t offset, u64 ino, unsigned int d_type) {

  struct dir_data *data = container_of(ctx, struct dir_data, ctx);

  if (!strcmp(name, data->input_name)) {
    data->result = 1;
  }

  return 0;
}

static inline int fs_entry_exist(const char *path)
{
  char parent[FS_MAX_PATH] = "\0";
  char child[FS_MAX_PATH] = "\0";

  fs_split_path(path, parent, child);
  struct file* filp = filp_open(parent, O_RDONLY, 0);
  struct dir_data data = {
	  .ctx.actor = _fs_find_entry,
	  .input_name = child,
    .result = 0,
  };
  iterate_dir(filp, &data.ctx);
  return data.result;
}

static inline int fs_os_procfs_entry_exist(int os_index)
{
  char path[FS_MAX_PATH] = "\0";
  sprintf(path, "/proc/mcos%d", os_index);
  return fs_entry_exist(path);
}

static inline int fs_os_sysfs_entry_exist(int os_index)
{
  char path[FS_MAX_PATH] = "\0";
  sprintf(path, "/sys/devices/virtual/mcos/mcos%d/sys", os_index);
  return fs_entry_exist(path);
}

#endif
