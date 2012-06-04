#ifndef __PARAM_H__
#define __PARAM_H__

#ifndef MAXPATHLEN
#ifdef PATH_MAX
#define MAXPATHLEN PATH_MAX
#else
#define MAXPATHLEN 255
#endif
#endif

#endif