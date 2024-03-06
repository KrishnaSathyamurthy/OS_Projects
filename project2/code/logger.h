#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#if defined(DEBUG_TL)
#include <stdio.h>
#endif

#ifdef DEBUG_TL
#define L_DEBUG_OUT(func, line, level, string)                                 \
  fprintf(stderr, "%s::%d[%s]::%s\n", func, line, level, string)
#define L_DEBUG_OUT_ARG(func, line, level, string, arg)                        \
  fprintf(stderr, "%s::%d[%s]::%s:%i\n", func, line, level, string, arg)
#define DEBUG_OUT(string) L_DEBUG_OUT(__FUNCTION__, __LINE__, "DEBUG", string)
#define DEBUG_OUT_ARG(string, arg)                                             \
  L_DEBUG_OUT_ARG(__FUNCTION__, __LINE__, "DEBUG", string, arg)
#else
#define DEBUG_OUT(string)
#define DEBUG_OUT_ARG(string, arg)
#endif
