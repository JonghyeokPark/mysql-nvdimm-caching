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

#define NVDIMM_PMDK_FILE_NAME "nvdimm_pmdk_file"
#define PMEM_MAX_FILE_NAME_LENGTH 10000

#define PMEMPMDK_INFO_PRINT(fmt, args...)              \
  fprintf(stderr, "[PMEMMMAP_INFO]: %s:%d:%s():" fmt,  \
  __FILE__, __LINE__, __func__, ##args)                \

#define PMEMPMDK_ERROR_PRINT(fmt, args...)             \
  fprintf(stderr, "[PMEMMMAP_ERROR]: %s:%d:%s():" fmt, \
  __FILE__, __LINE__, __func__, ##args)                \


/* pmem wrapper function */
struct __pmem_wrapper;
typedef struct __pmem_wrapper PMEM_WRAPPER;

struct __pmem_pmtrlog_buf;
typedef struct __pmem_pmtrlog_buf PMEM_MTRLOG_BUF;

struct __pmem_pmtrlog_hdr;
typedef struct __pmem_pmtrlog_hdr PMEM_MTRLOG_HDR;

#define PMEM_MTRLOG_HDR_SIZE sizeof(PMEM_MTRLOG_HDR)

POBJ_LAYOUT_BEGIN(my_pmemobj);
POBJ_LAYOUT_TOID(my_pmemobj, char);
POBJ_LAYOUT_TOID(my_pmemobj, TOID(char));
POBJ_LAYOUT_TOID(my_pmemobj, PMEM_MTRLOG_BUF);
POBJ_LAYOUT_END(my_pmemobj);

/* global wrapper */
struct __pmem_wrapper {
	char name[PMEM_MAX_FILE_NAME_LENGTH];
	PMEMobjpool *pobjp;
	// mini transaction log buf
	PMEM_MTRLOG_BUF *pmtrlogbuf;
	bool is_new;
};

/* pm_wrapper function */
PMEM_WRAPPER* pm_wrapper_create(const char* path, const size_t pool_size);
void pm_wrapper_free(PMEM_WRAPPER* pmw);
PMEMoid pm_pobjp_alloc_bytes(PMEMobjpool* pobjp, size_t size);
void pm_pobjp_free(PMEMobjpool* pop);


/* mini-transaction logging */
struct __pmem_pmtrlog_buf {
	PMEMmutex mutex;          // synchronization for accessing mtr log buffer
  size_t size; 							// mtr_log area size
	size_t cur_offset;				// mtr_log current offset 
  size_t reusable_offset;   // latest reusable log offset
                            // until this offset we can reuse mtr log area
	PMEMoid data; 						// mtr log data
};

struct __pmem_pmtrlog_hdr {
  unsigned long int len;    // mtr_log length
  unsigned long int lsn;    // start_lsn of log handler when this mtrlog is written
  size_t prev;              // prev mtr log header offset 
  size_t next;              // next mtr log header offset
  bool need_recv;           // recovery flag
                            // 0: no need to recovery, we can reuse this mtr log space
                            // 1: need to recvoery
};

int pm_wrapper_mtrlogbuf_alloc(PMEM_WRAPPER* pmw, const size_t size);
PMEM_MTRLOG_BUF* pm_pobjp_mtrlogbuf_alloc(PMEMobjpool* pobjp, const size_t size);
int pm_wrapper_logbuf_realloc(PMEM_WRAPPER* pmw, const size_t size);
PMEM_MTRLOG_BUF* pm_pobjp_mtrlogbuf_realloc(PMEMobjpool* pobjp, const size_t size);
PMEM_MTRLOG_BUF* pm_pobjp_get_mtrlogbuf(PMEMobjpool* pobjp);
void* pm_wrapper_mtrlogbuf_get_logdata(PMEM_WRAPPER* pmw);
ssize_t pm_wrapper_mtrlogbuf_write(PMEM_WRAPPER* pmw, const uint8_t* buf, unsigned long int n,
                                   unsigned long int lsn);


#endif
