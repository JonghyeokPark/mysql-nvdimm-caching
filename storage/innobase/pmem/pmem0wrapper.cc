#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>

#include "pmem_pmdk_obj.h"

// global PMEM_WRAPPER
PMEM_WRAPPER* gb_pmw = NULL;

PMEM_WRAPPER* pm_wrapper_create(const char *path, const size_t pool_size) {
	PMEM_WRAPPER* pmw;
	PMEMobjpool* pobjp = NULL;
	int ret = 0;

	pmw = (PMEM_WRAPPER*) malloc(sizeof(PMEM_WRAPPER));
	if(!pmw) {
    PMEMPMDK_ERROR_PRINT("pm_wrapper_create failed\n");
		return NULL;
	}
	
	// create or open the pmemobj pool
	if (access(path, F_OK) != 0) {
		// toggle the initialize bit
		pmw->is_new = true;
		pobjp = pmemobj_create(path, POBJ_LAYOUT_NAME(my_pmemobj),pool_size,0666);
		if (pobjp==NULL) {
      PMEMPMDK_ERROR_PRINT("Failed to create pmemobj pool (%s)\n", pmemobj_errormsg());
		} 
	} else {
		pmw->is_new = false;
		if((pobjp = pmemobj_open(path, POBJ_LAYOUT_NAME(my_pmemobj))) == NULL) {
      PMEMPMDK_ERROR_PRINT("Failed to open pmemobj pool(%s)\n", pmemobj_errormsg());
			if (pmw) {
				pm_wrapper_free(pmw);
				return NULL;
			}
		}
	}

	size_t mapped_size = 0;
	void * Addr = pmem_map_file(path, 0, 0, 0, &mapped_size, NULL);
	// TODO(jhpark): PMDK has crucial error on pmem_is_pmem on dax enabled filesystem.
	//							 Linux kernel 4.9 test passed.
	ret = pmem_is_pmem(Addr , mapped_size);
	if (ret) {
    PMEMPMDK_ERROR_PRINT("pmem_is_pemm failed\n");
    PMEMPMDK_INFO_PRINT("Current kernel does not recognize NVDIMM as the persistent memory \
                        We force to set the environment PMEM_IS_PMEM_FORCE value \
                        Then PMDK API call mfense() instead of msync()");
	}
	// Force to set NVDIMM 
	setenv("PMEM_IS_PMEM_FORCE", "1", 1);

	pmw->pobjp = pobjp;
	strncpy(pmw->name, path, PMEM_MAX_FILE_NAME_LENGTH);
	pmw->name[PMEM_MAX_FILE_NAME_LENGTH-1] = '\0';

	// mini transaction log buffer allocation
	pmw->pmtrlogbuf = NULL;

	if (!pmw->is_new) {
		pmw->pmtrlogbuf = pm_pobjp_get_mtrlogbuf(pobjp);
		if (!pmw->pmtrlogbuf) {
      PMEMPMDK_ERROR_PRINT("pmtrlogbuf is empty\n");
		}
	}
	return pmw;
}

void pm_wrapper_free(PMEM_WRAPPER* pmw) {
	if (pmw->pobjp) {
		pm_pobjp_free(pmw->pobjp);
	}

	pmw->pmtrlogbuf = NULL;
	pmw->pobjp = NULL;
  PMEMPMDK_INFO_PRINT("Free PMEM_WRAPPER from heap allocator\n");
	free(pmw);
}

/* allocate a range of persitent memory 
 * @param[in] pop   PMEMobjpool object 
 * @param[in] size  size of allocation 
 * @return persistent memory oid */
PMEMoid pm_pobjp_alloc_bytes(PMEMobjpool* pobjp, size_t size) {
  TOID(char) array;
  POBJ_ALLOC(pobjp, &array, char, sizeof(char) * size, NULL, NULL);

  if (TOID_IS_NULL(array)) {
    PMEMPMDK_ERROR_PRINT("Failed to allocate\n");
    return OID_NULL;
  }

  pmemobj_persist(pobjp, D_RW(array), size*sizeof(*D_RW(array)));
  PMEMPMDK_INFO_PRINT("Allocate PMEMobjpool from pmem with size %zu MB\n", (size/1024*1024));
  
  char *p = (char*) pmemobj_direct(array.oid);
  if (!p) {
    PMEMPMDK_ERROR_PRINT(" msg: (%s)\n", pmemobj_errormsg());
  }
  return array.oid;
}

/* free a range of persistent memory
 * @param[in] pop PMEMobjpool object */
void pm_pobjp_free(PMEMobjpool* pobjp) {
  TOID(PMEM_MTRLOG_BUF) mtrlogbuf;
  TOID(char) data1;

  // free the log buffer
  POBJ_FOREACH_TYPE(pobjp, mtrlogbuf) {
    TOID_ASSIGN(data1, D_RW(mtrlogbuf)->data);
    POBJ_FREE(&data1);
    
		D_RW(mtrlogbuf)->size = 0;
    //D_RW(mtrlogbuf)->need_recv = false;

    POBJ_FREE(&mtrlogbuf);
  }
  PMEMPMDK_INFO_PRINT("Free PMEMobjpool from persistent memory\n");
  pmemobj_close(pobjp);
}
