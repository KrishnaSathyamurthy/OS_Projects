#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif

#if defined(DEBUG_TL) || defined(INFO_TL)
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

#ifdef INFO_TL
#define L_INFO_OUT(func, line, level, string)                                  \
  fprintf(stdout, "%s::%d[%s]::%s\n", func, line, level, string)
#define L_INFO_OUT_ARG(func, line, level, string, arg)                         \
  fprintf(stdout, "%s::%d[%s]::%s:%i\n", func, line, level, string, arg)
#define INFO_OUT(string) L_INFO_OUT(__FUNCTION__, __LINE__, "INFO", string)
#define INFO_OUT_ARG(string, arg)                                              \
  L_INFO_OUT_ARG(__FUNCTION__, __LINE__, "INFO", string, arg)
#else
#define INFO_OUT(string)
#define INFO_OUT_ARG(string, arg)
#endif
