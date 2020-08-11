#ifndef __OKNG_H_INCLUDED__
#define __OKNG_H_INCLUDED__

#ifndef PRINT
  #define PRINT printf
#endif

#define _OKNG(jump, cond, fmt, args...) do {  \
  if (cond) {                                 \
    PRINT("[ IHK ] [  OK  ] " fmt, ##args);   \
  } else {                                    \
    PRINT("[ IHK ] [  NG  ] " fmt, ##args);   \
    if (jump)                                 \
      goto err;                               \
  }                                           \
} while (0)

#define OKNG(args...) _OKNG(1, ##args)
#define INFO(fmt, args...) PRINT("[ IHK ] [ INFO ] " fmt, ##args)
#define START(case_name) PRINT("[ IHK ] [ START ] test-func: %s: %s\n", __FUNCTION__, case_name)
#define INTERR(cond, fmt, args...) do {                           \
  if (cond) {                                                     \
    char msg[4096];                                               \
    sprintf(msg, fmt, ##args);                                    \
    PRINT("[ IHK ] [INTERR] %s:%d %s", __FILE__, __LINE__, msg);  \
    ret = -1;                                                     \
    goto out;                                                     \
  }                                                               \
} while (0)

#endif
