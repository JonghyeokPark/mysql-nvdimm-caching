#include "pmem_mmap_obj.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>


// gloabl persistent memmory region
char* gb_pm_mmap;
int gb_pm_mmap_fd;

char* pm_mmap_create(const char* path, const uint64_t pool_size) {
  
  if (access(path, F_OK) != 0) {
    gb_pm_mmap_fd = open(path, O_RDWR | O_CREAT, 0777); 
    if (gb_pm_mmap_fd < 0) {
      PMEMMMAP_ERROR_PRINT("pm_mmap_file open failed\n");
      return NULL;
    }

    // set file size as pool_size
    if (truncate(path, pool_size) == -1) {
      PMEMMMAP_ERROR_PRINT("pm_mmap_file turncate failed\n");
    }

    gb_pm_mmap = (char *) mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, gb_pm_mmap_fd, 0);
    if (gb_pm_mmap == MAP_FAILED) {
      PMEMMMAP_ERROR_PRINT("pm_mmap mmap() failed\n");
    }
    memset(gb_pm_mmap, 0x00, pool_size);
  } else {
    // TODO(jhaprk) recvoery setup
    gb_pm_mmap = (char *) mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, gb_pm_mmap_fd, 0);
    if (gb_pm_mmap == MAP_FAILED) {
      PMEMMMAP_ERROR_PRINT("pm_mmap mmap() faild recovery failed\n");
    } 
  }

  // Force to set NVIMMM
  setenv("PMEM_IS_PMEM_FORCE", "1", 1);
  PMEMMMAP_INFO_PRINT("Current kernel does not recognize NVDIMM as the persistenct memory \
      We force to set the environment variable PMEM_IS_PMEM_FORCE \
      We call mync() instead of mfense()\n");

  return gb_pm_mmap;
}

void pm_mmap_free(const uint64_t pool_size) {
 munmap(gb_pm_mmap, pool_size);
 close(gb_pm_mmap_fd);
 PMEMMMAP_INFO_PRINT("munmap persistent memroy region\n");
}

// allocate mtr log buffer
int pm_mmap_mtrlogbuf_alloc(const size_t size) {

  PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf;
  mmap_mtrlogbuf = (PMEM_MMAP_MTRLOG_BUF*) malloc(sizeof(PMEM_MMAP_MTRLOG_BUF));

  // TODO(jhpark): initialize mutex
  mmap_mtrlogbuf->size = size;
  mmap_mtrlogbuf->cur_offset = 0;
  mmap_mtrlogbuf->reusable_offset = 0;
  mmap_mtrlogbuf->data = (char *)malloc(size);

  memcpy_persist(gb_pm_mmap, mmap_mtrlogbuf, sizeof(PMEM_MMAP_MTRLOG_BUF));
  free(mmap_mtrlogbuf);
  return 0;
}

// write mtr log
ssize_t pm_mmap_mtrlogbuf_write( 
    const uint8_t* buf,
    unsigned long int n,
    unsigned long int lsn) 
{
  unsigned long int ret = 0;

  // TODO(jhpark) : mutex add
  PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf = (PMEM_MMAP_MTRLOG_BUF*) gb_pm_mmap;
  char* pdata = (char *) mmap_mtrlogbuf->data;
  
  // (jhpark): original version of mtr logging relies on external offset value
  //           which is persisted on every updates
  size_t offset = mmap_mtrlogbuf->cur_offset;

  // Fill the mmap_mtr log header info.
  PMEM_MMAP_MTRLOG_HDR* mmap_mtr_hdr = (PMEM_MMAP_MTRLOG_HDR*) malloc(PMEM_MMAP_MTRLOG_HDR_SIZE);
  mmap_mtr_hdr->len = n;
  mmap_mtr_hdr->lsn = lsn;
  mmap_mtr_hdr->prev = offset;

  // Check free space in mmap_mtr log area
  PMEM_MMAP_MTRLOG_HDR* cur_mmap_mtr_hdr = (PMEM_MMAP_MTRLOG_HDR*)(pdata + offset);
  size_t needed_space = PMEM_MMAP_MTRLOG_HDR_SIZE + n;
  size_t total_mmap_mtrlog_area_size = mmap_mtrlogbuf->size;
  
  // TODO(jhpark): how to clear mtr log records in mtr logging region
  if ( (total_mmap_mtrlog_area_size - cur_mmap_mtr_hdr->next) < needed_space ) {   
    offset = 0;
  }
  mmap_mtr_hdr->next = offset + PMEM_MMAP_MTRLOG_HDR_SIZE +n; 
  mmap_mtr_hdr->need_recv = true;

#ifdef UNIV_LOG_HEADER
  // log << header << persist(log) << payload << persist(log)
  // (jhpark) : header version brought from 
  //            [Persistent Memory I/O Primitives](https://arxiv.org/pdf/1904.01614.pdf)
  
  // write header + payload
  volatile int org_offset = offset;
  memcpy(pdata+offset, mmap_mtr_hdr, (size_t)PMEM_MMAP_MTRLOG_HDR_SIZE);
  offset += PMEM_MMAP_MTRLOG_HDR_SIZE;
  memcpy(pdata+offset, buf, (size_t)n);
  // persistent barrier
  flush_cache(pdata+org_offset, (size_t)(PMEM_MMAP_MTRLOG_HDR_SIZE+n));
  // record log size
  mmap_mtrlogbuf->size = (size_t)(PMEM_MMAP_MTRLOG_HDR_SIZE+n);
  // persistent barrier 
  flush_cache(&mmap_mtrlogbuf->size, sizeof(mmap_mtrlogbuf->size)); 
  
  // (jhpark) : original header version
  // write header
  //memcpy_persist(pdata+offset, mmap_mtr_hdr, (size_t)PMEM_MMAP_MTRLOG_HDR_SIZE);
  //offset += PMEM_MMAP_MTRLOG_HDR_SIZE;
  // write payload
  //memcpy_persist(pdata+offset, (void*)buf, (size_t)n);
  //mmap_mtrlogbuf->cur_offset = offset + n;
  //flush_cache(&mmap_mtrlogbuf->cur_offset, sizeof(mmap_mtrlogbuf->cur_offset));
#elif UNIV_LOG_ZERO
  // log << header << cnt << payload << persist(log)
  volatile int org_offset = offset;
  memcpy(pdata+offset, mmap_mtr_hdr, (size_t)PMEM_MMAP_MTRLOG_HDR_SIZE);
  offset += PMEM_MMAP_MTRLOG_HDR_SIZE;
  int cnt = __builtin_popcount((uint64_t)buf);
  memcpy(pdata+offset, &cnt, sizeof(cnt));
  offset += sizeof(cnt);
  memcpy(pdata+offset, buf, (size_t)n);
  // persistent barrier
  flush_cache(pdata+org_offset, (size_t)(PMEM_MMAP_MTRLOG_HDR_SIZE+sizeof(cnt)+n));
  mmap_mtrlogbuf->cur_offset = offset + n;
  // persistent barrier (for now, just ignore)
  //flush_cache(&mmap_mtrlogbuf->cur_offset, sizeof(mmap_mtrlogbuf->cur_offset));
#endif

  free(mmap_mtr_hdr);
  ret = n;
  return ret;
}
