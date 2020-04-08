#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>                                                                      
#include <sys/time.h> //for struct timeval, gettimeofday()
#include <string.h>
#include <stdint.h>   //for uint64_t
#include <math.h>     //for log()
#include <assert.h>
#include <wchar.h>
#include <unistd.h>   //for access()

#include "pmem_pmdk_obj.h"

/* allocate the mtrlogbuf */
int pm_wrapper_mtrlogbuf_alloc(PMEM_WRAPPER* pmw, const size_t size) {
	assert(pmw);
	pmw->pmtrlogbuf = pm_pobjp_mtrlogbuf_alloc(pmw->pobjp, size);
	if (!pmw->pmtrlogbuf) {
		return -1;
  }
	else {
		return 0;
  }
}

/*
 * Allocate new mtrlog buffer in persistent memory and assign to the pointer in the wrapper
 * @param[in] pobjp: 	PMEMObjpool
 * @param[in] size:	mtr log size
 * */
PMEM_MTRLOG_BUF* pm_pobjp_mtrlogbuf_alloc(PMEMobjpool* pobjp, const size_t size) {

  TOID(PMEM_MTRLOG_BUF) mtrlogbuf; 
	POBJ_ZNEW(pobjp, &mtrlogbuf, PMEM_MTRLOG_BUF); // zalloc

	PMEM_MTRLOG_BUF *pmtrlogbuf = D_RW(mtrlogbuf);
  // initialzize the mutex
  pmemobj_mutex_zero(pobjp, &pmtrlogbuf->mutex);
	// initialize the mini trx log area size
	pmtrlogbuf->size = size;
	// initialize the current offset in mtr log area
	pmtrlogbuf->cur_offset = 0;
	// intialize the reusable offset in mtr log area 
	pmtrlogbuf->reusable_offset = 0;
	// allocate the data
	pmtrlogbuf->data = pm_pobjp_alloc_bytes(pobjp, size);
	if (OID_IS_NULL(pmtrlogbuf->data)){
		return NULL;
	}
	pmemobj_persist(pobjp, pmtrlogbuf, sizeof(*pmtrlogbuf));
	return pmtrlogbuf;
} 

/*
 * Reallocate mtr log buffer 
 * @param[in] pmw: pmem wrapepr
 * @param[in] size: mtr log size 
 * */
int pm_wrapper_logbuf_realloc(PMEM_WRAPPER* pmw, const size_t size) {
	assert(pmw);
	pmw->pmtrlogbuf = pm_pobjp_mtrlogbuf_realloc(pmw->pobjp, size);
	if (!pmw->pmtrlogbuf)
		return -1;
	else
		return 0;
}

/*
 * Re-Allocate mtr log buffer in persistent memory and assign to the pointer in the wrapper
 * We can use POBJ_REALLOC. This version use free + alloc
 * */
PMEM_MTRLOG_BUF* pm_pobjp_mtrlogbuf_realloc(PMEMobjpool* pobjp, const size_t size) {
	TOID(PMEM_MTRLOG_BUF) mtrlogbuf; 
	TOID(char) data;

	/* get the old data and free it */
	mtrlogbuf = POBJ_FIRST(pobjp, PMEM_MTRLOG_BUF);
	PMEM_MTRLOG_BUF *pmtrlogbuf = D_RW(mtrlogbuf);
	
	TOID_ASSIGN(data, pmtrlogbuf->data);
	POBJ_FREE(&data);	
	
	/* allocate a new one */	
	pmtrlogbuf->data = pm_pobjp_alloc_bytes(pobjp, size);
	if (OID_IS_NULL(pmtrlogbuf->data)){
		return NULL;
	}

	pmemobj_persist(pobjp, pmtrlogbuf, sizeof(*pmtrlogbuf));
	return pmtrlogbuf;
} 

/* get the mtr log buffer */
PMEM_MTRLOG_BUF* pm_pobjp_get_mtrlogbuf(PMEMobjpool* pobjp) {
	TOID(PMEM_MTRLOG_BUF) mtrlogbuf;
	//get the first object in pmem has type PMEM_MTRLOG_BUF
	mtrlogbuf = POBJ_FIRST(pobjp, PMEM_MTRLOG_BUF);

	if (TOID_IS_NULL(mtrlogbuf)) {
		return NULL;
	}	else {
		PMEM_MTRLOG_BUF *pmtrlogbuf = D_RW(mtrlogbuf);
		if(!pmtrlogbuf) {
      PMEMPMDK_ERROR_PRINT("msg: (%s)", pmemobj_errormsg());
			return NULL;
		}
		return pmtrlogbuf;
	}
}

void* pm_wrapper_mtrlogbuf_get_logdata(PMEM_WRAPPER* pmw){
	assert(pmw);
	assert(pmw->pmtrlogbuf);
	return pmemobj_direct(pmw->pmtrlogbuf->data);
}

ssize_t pm_wrapper_mtrlogbuf_write( 
		PMEM_WRAPPER* pmw,								// pmem wrapper
		const uint8_t* buf, 							// mtr log buffer to write in
		unsigned long int n,                          // number of bytes to write
    unsigned long int lsn) 							          // lsn
///////////////////////////////////
{
	unsigned long int ret = 0;
	if (!pmw->pmtrlogbuf) {
    PMEMPMDK_ERROR_PRINT("pmtrlogbuf is NULL\n");
		return -1;
	}
  
  // CRITICAL SECTION //
  // acquire the mutex (become bottle neck)
  pmemobj_mutex_lock(pmw->pobjp, &pmw->pmtrlogbuf->mutex);

#ifdef UNIV_PMDK_PERSISTENT
  // (jhpark): In this mode, PMDK only guarantees "persistency".
  //           Do not use the PMDK own undo log for guaranteeing atomicity.

	char *pdata = (char*) pmemobj_direct(pmw->pmtrlogbuf->data);
	size_t offset = pmw->pmtrlogbuf->cur_offset;

  // TODO(jhpark) : eagarly check the reusable offset
  //                if reuseble offset > mtr log area size then check again from 0
  //                For now, we ignore the recovery so just perform cyclic logging

  // Fill the mtr log header info.
  PMEM_MTRLOG_HDR *mtr_hdr = (PMEM_MTRLOG_HDR*)malloc(PMEM_MTRLOG_HDR_SIZE);
  mtr_hdr->len = n;
  mtr_hdr->lsn = lsn;
  mtr_hdr->prev = offset;

  // check free space in mtr log area
  PMEM_MTRLOG_HDR *cur_mtr_hdr = (PMEM_MTRLOG_HDR*)(pdata+offset);

  size_t needed_space = PMEM_MTRLOG_HDR_SIZE + n;
  size_t total_mtrlog_area_size = pmw->pmtrlogbuf->size;

  // TODO(jhpark): After transaction finished to write page into the NVDIMM
  //               we need to distingiush which mtr log can be removed. 
  if ( (total_mtrlog_area_size - cur_mtr_hdr->next) < needed_space ) {
    offset = 0;  
  } 
     
  mtr_hdr->next = offset + PMEM_MTRLOG_HDR_SIZE +n;
  mtr_hdr->need_recv = true;

  // write header
  pmemobj_memcpy_persist(pmw->pobjp, pdata + offset, mtr_hdr, (size_t) PMEM_MTRLOG_HDR_SIZE);
  offset += PMEM_MTRLOG_HDR_SIZE;
  
  // write data
  pmemobj_memcpy_persist(pmw->pobjp, pdata + offset, buf, (size_t) n);
  pmw->pmtrlogbuf->cur_offset = offset + n;
  pmemobj_persist(pmw->pobjp, &pmw->pmtrlogbuf->cur_offset, sizeof(pmw->pmtrlogbuf->cur_offset)); 
  
  ret = n;
#else

	TX_BEGIN(pmw->pobjp) {
		char *pdata = (char*) pmemobj_direct(pmw->pmtrlogbuf->data);
		size_t offset = pmw->pmtrlogbuf->cur_offset;

    // TODO(jhpark) : eagarly check the reusable offset
    //                if reuseble offset > mtr log area size then check again from 0
    //                For now, we ignore the recovery so just perform cyclic logging

    // Fill the mtr log header info.
    PMEM_MTRLOG_HDR *mtr_hdr = (PMEM_MTRLOG_HDR*)malloc(PMEM_MTRLOG_HDR_SIZE);
    mtr_hdr->len = n;
    mtr_hdr->lsn = lsn;
    mtr_hdr->prev = offset;

    // check free space in mtr log area
    PMEM_MTRLOG_HDR *cur_mtr_hdr = (PMEM_MTRLOG_HDR*)(pdata+offset);

    size_t needed_space = PMEM_MTRLOG_HDR_SIZE + n;
    size_t total_mtrlog_area_size = pmw->pmtrlogbuf->size;

    // TODO(jhpark): After transaction finished to write page into the NVDIMM
    //               we need to distingiush which mtr log can be removed. 
    if ( (total_mtrlog_area_size - cur_mtr_hdr->next) < needed_space ) {
      offset = 0;  
    } 
     
    mtr_hdr->next = offset + PMEM_MTRLOG_HDR_SIZE +n;
    mtr_hdr->need_recv = true;

    // write header
    pmemobj_memcpy_persist(pmw->pobjp, pdata + offset, mtr_hdr, (size_t) PMEM_MTRLOG_HDR_SIZE);
    offset += PMEM_MTRLOG_HDR_SIZE;

    // write data
		pmemobj_memcpy_persist(pmw->pobjp, pdata + offset, buf, (size_t) n);
    pmw->pmtrlogbuf->cur_offset = offset + n;
    pmemobj_persist(pmw->pobjp, &pmw->pmtrlogbuf->cur_offset,sizeof(pmw->pmtrlogbuf->cur_offset)); 

		ret = n;
	} TX_END;

#endif /* UNIV_PMDK_PERSISTENT */

  // acquire the mutex (become bottle neck)
  pmemobj_mutex_unlock(pmw->pobjp, &pmw->pmtrlogbuf->mutex);
  // CRITICAL SECTION //

	return ret;
}

