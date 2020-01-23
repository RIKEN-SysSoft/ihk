#ifndef __LINUX_H_INCLUDED__
#define __LINUX_H_INCLUDED__

int _linux_insmod(char *fn, char *opts);
int linux_insmod(int verbose);
int linux_chmod(int dev_index);
int linux_wait_chmod(int dev_index);
int _linux_rmmod(char *fn);
int linux_rmmod(int verbose);
int linux_kill_mcexec(void);

#endif
