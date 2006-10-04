#include <stdio.h>

#define ERR(fmt, args...) fprintf(stderr, fmt, ## args)
#define DBG2(fmt, args...) fprintf(stderr, "%s:%s:%-5d: " fmt, __FILE__,  __FUNCTION__, __LINE__, ## args)
#define ENTER2          do { fprintf(stderr, "%s:%s:%-5d: ENTER\n",  __FILE__,__FUNCTION__, __LINE__); } while(0)
#define RET2(args...)   do { fprintf(stderr, "%s:%s:%-5d: RETURN\n",  __FILE__,__FUNCTION__, __LINE__);\
return args; } while(0)

enum { LOG_NONE, LOG_ERR, LOG_WARN, LOG_INFO, LOG_ALL };
#ifdef DEBUG

#define ENTER          do { fprintf(stderr, "%s:%s:%-5d: ENTER\n",  __FILE__,__FUNCTION__, __LINE__); } while(0)
#define RET(args...)   do { fprintf(stderr, "%s:%s:%-5d: RETURN\n", __FILE__, __FUNCTION__, __LINE__);\
return args; } while(0)
#define DBG(fmt, args...) fprintf(stderr, "%s:%s:%-5d: " fmt,  __FILE__,__FUNCTION__, __LINE__, ## args)
#define LOG(level, fmt, args...) fprintf(stderr, fmt, ## args)

#else

extern int log_level;
#define ENTER         do {  } while(0)
#define RET(args...)   return args
#define DBG(fmt, args...)   do {  } while(0)
#define LOG(level, fmt, args...) do { if (level <= log_level) fprintf(stderr, fmt, ## args); } while(0)

#endif

