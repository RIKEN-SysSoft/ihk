#ifndef __OS_H_INCLUDED__
#define __OS_H_INCLUDED__

int os_load(void);
int os_kargs(void);
int os_wait_for_status(int status);

#endif
