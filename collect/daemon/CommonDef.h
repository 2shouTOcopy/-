#pragma once

#ifndef DEFAULT_LOAD_SO_PATH
#define DEFAULT_LOAD_SO_PATH     PREFIX "/modules"   //默认动态库加载路径
#endif

#ifndef PACKAGE_NAME
#define PACKAGE_NAME "collect"
#endif

#ifndef PREFIX
#define PREFIX "/mnt/data/" PACKAGE_NAME
#endif

#ifndef SYSCONFDIR
#define SYSCONFDIR PREFIX "/share"
#endif

#ifndef CONFIGFILE
#define CONFIGFILE SYSCONFDIR "/collect.conf"
#endif

#ifndef PIDFILE
#define PIDFILE PREFIX "/" PACKAGE_NAME ".pid"
#endif

class CAbstractUserModule;
typedef void (*pfnDestroyModule)(CAbstractUserModule* pUserModule);
typedef CAbstractUserModule* (*pfnCreateModule)();

typedef uint64_t cdtime_t;

