#ifndef __PMEMMMAPOBJ_H_
#define __PMEMMMAPOBJ_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>

// memory allocator
#include <memory>

// (jhpark): this header file for UNIV_NVDIMM_CACHE
//					 use persistent memroy with mmap on dax-enabled file system
//					 numoerous data structures and functions are silimilar to 
//					 ones in pmem_obj.h

// TODO(jhpark) : separate cmopile option (-DUNIV_PMEM_MMAP)
// TODO(jhpark) : redesign for configurable NVDIMM caching options

#define NVDIMM_MMAP_FILE_NAME         "nvdimm_mmap_file"
#define PMEM_MMAP_MAX_FILE_NAME_LENGTH    10000

#define PMEMMMAP_INFO_PRINT(fmt, args...)              \
	fprintf(stderr, "[PMEMMMAP_INFO]: %s:%d:%s():" fmt,  \
	__FILE__, __LINE__, __func__, ##args)             	 \

#define PMEMMMAP_ERROR_PRINT(fmt, args...)             \
	fprintf(stderr, "[PMEMMMAP_ERROR]: %s:%d:%s():" fmt, \
	__FILE__, __LINE__, __func__, ##args)             	 \

// pmem_persistent 
#define CACHE_LINE_SIZE 64
#define ASMFLUSH(dst) __asm__ __volatile__ ("clflush %0" : : "m"(*(volatile char *)dst))

static inline void clflush(volatile char* __p) {   
	asm volatile("clflush %0" : "+m" (*__p));
}

static inline void mfence() {   
	asm volatile("mfence":::"memory");
  return;
}

static inline void flush_cache(void *ptr, size_t size){
  unsigned int  i=0;
  uint64_t addr = (uint64_t)ptr;
    
  mfence();
  for (i =0; i < size; i=i+CACHE_LINE_SIZE) {
    clflush((volatile char*)addr);
    addr += CACHE_LINE_SIZE;
  }
  mfence();
}
static inline void memcpy_persist
                    (void *dest, void *src, size_t size){
  unsigned int  i=0;
  uint64_t addr = (uint64_t)dest;

  memcpy(dest, src, size);

  mfence();
  for (i =0; i < size; i=i+CACHE_LINE_SIZE) {
    clflush((volatile char*)addr);
    addr += CACHE_LINE_SIZE;
  }
  mfence();
}

struct __pmem_mmap_mtrlog_buf;
typedef struct __pmem_mmap_mtrlog_buf PMEM_MMAP_MTRLOG_BUF;

struct __pmem_mmap_mtrlog_hdr;
typedef struct __pmem_mmap_mtrlog_hdr PMEM_MMAP_MTRLOG_HDR;

#define PMEM_MMAP_MTRLOG_HDR_SIZE sizeof(PMEM_MMAP_MTRLOG_HDR)

/* PMEM_MMAP mtrlog file header */

// (jhpark): more offset will be added if recovery algorithm fixed.
// recovery flag
constexpr uint32_t PMEM_MMAP_RECOVERY_FLAG = 0;
// mtrlog checkpoint lsn; recovery start from this lsn (i.e., offset)
constexpr uint32_t PMEM_MMAP_LOGFILE_CKPT_LSN = 1;
// PMEM_MMAP mtrlog file header size
constexpr uint32_t PMEM_MMAP_LOGFILE_HEADER_SZ = PMEM_MMAP_RECOVERY_FLAG
                                             + PMEM_MMAP_LOGFILE_CKPT_LSN;

/////////////////////////////////////////////////////////

// wrapper function (see pemm_obj.h)
// mmap persistent memroy region on dax file system
char* pm_mmap_create(const char* path, const uint64_t pool_size);
// unmmap persistent memory 
void pm_mmap_free(const uint64_t pool_size);

/* mtr log */
// data structure in pmem_obj
struct __pmem_mmap_mtrlog_buf {
	// TODO(jhpark) add mutex -- pthread lock vs. mcs lock
	size_t size;
	size_t cur_offset;
	size_t reusable_offset;
	char* data;
};

struct __pmem_mmap_mtrlog_hdr {
	unsigned long int len;
	unsigned long int lsn;
	size_t prev;
	size_t next;
	bool need_recv;
};

// logging? 
int pm_mmap_mtrlogbuf_alloc(const size_t size);
ssize_t pm_mmap_mtrlogbuf_write(const uint8_t* buf, 
                                unsigned long int n, unsigned long int lsn);

// (jhpark):  Custom memory allocator with aligned. 
//            For current pmem_mmap version doesn't use this technique.
//            references: https://docs.w3cub.com/cpp/memory/align/
template <std::size_t N>
struct MyAllocator
{
    char data[N];
    void* p;
    std::size_t sz;
    MyAllocator() : p(data), sz(N) {}
    template <typename T>
    T* aligned_alloc(std::size_t a = alignof(T))
    {
        if (std::align(a, sizeof(T), p, sz))
        {
            T* result = reinterpret_cast<T*>(p);
            p = (char*)p + sizeof(T);
            sz -= sizeof(T);
            return result;
        }
        return nullptr;
    }
};

// CACHE_LINE_SIZE aligned memcpy
inline char* aligned_memcpy (char* dst, const char* src, size_t len) {
  size_t i;
  if (len >= CACHE_LINE_SIZE) {
    i = len / CACHE_LINE_SIZE;
    len &= (CACHE_LINE_SIZE-1);
    while (i-- > 0) {
      __asm__ __volatile__( 
          "movdqa (%0), %%xmm0\n"
          "movdqa 16(%0), %%xmm1\n"
          "movdqa 32(%0), %%xmm2\n"
          "movdqa 48(%0), %%xmm3\n"
          "movntps  %%xmm0, (%1)\n"
          "movntps  %%xmm1, 16(%1)\n"
          "movntps  %%xmm2, 32(%1)\n"
          "movntps  %%xmm3, 48(%1)\n"
          ::"r"(src), "r"(dst):"memory");
      dst += CACHE_LINE_SIZE;
      src += CACHE_LINE_SIZE;
    }
  }

  if (len) {
    memcpy(dst, src, len);
  }
  return dst;
}

#endif  /* __PMEMMAPOBJ_H__ */
