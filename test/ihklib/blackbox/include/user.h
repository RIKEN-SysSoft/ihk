#ifndef __USER_H_INCLUDED__
#define __USER_H_INCLUDED__

extern char **environ;

int __user_fork_exec(char *cmd, pid_t *pid);
int _user_fork_exec(char *filename, pid_t *pid, char *opt);
int user_fork_exec(char *filename, pid_t *pid);
int user_wait(pid_t *pid);
int user_poll_fifo(int fd_fifo, int max_count);

#endif
