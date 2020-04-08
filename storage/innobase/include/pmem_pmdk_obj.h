#ifndef __PMEMOBJ_H__
#define __PMEMOBJ_H__

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h> //for struct timeval, gettimeofday()

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <wchar.h>
#include <unistd.h>

#include <libpmem.h>
#include <libpmemobj.h>

#define NVDIMM_FILE_NAME "nvdimm_pmdk_file"

#define PMEMPMDK_INFO_PRINT(fmt, args...)              \
  fprintf(stderr, "[PMEMMMAP_INFO]: %s:%d:%s():" fmt,  \
  __FILE__, __LINE__, __func__, ##args)                \

#define PMEMPMDK_ERROR_PRINT(fmt, args...)             \
  fprintf(stderr, "[PMEMMMAP_ERROR]: %s:%d:%s():" fmt, \
  __FILE__, __LINE__, __func__, ##args)                \

#endif
