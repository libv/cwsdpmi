/* Copyright (C) 1995,1996 CW Sandmann (sandmann@clio.rice.edu) 1206 Braelinn, Sugarland, TX 77479
** Copyright (C) 1993 DJ Delorie, 24 Kirsten Ave, Rochester NH 03867-2954
**
** This file is distributed under the terms listed in the document
** "copying.cws", available from CW Sandmann at the address above.
** A copy of "copying.cws" should accompany this file; if not, a copy
** should be available from where this file was obtained.  This file
** may not be distributed without a verbatim copy of "copying.cws".
**
** This file is distributed WITHOUT ANY WARRANTY; without even the implied
** warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/
/* Modified for VCPI Implement by Y.Shibata Aug 5th 1991 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>

#include "gotypes.h"
#include "valloc.h"
#include "xms.h"
#include "vcpi.h"
#include "paging.h"
#include "control.h"
#include "mswitch.h"

#define NPAGEDIR CWSpar.pagedir /* In low memory * 4K, user gets 3 less, 4Mb/entry */
#define MINAPPMEM CWSpar.minapp	/* Min pages extended before we use DOS memory */
#define SAVEPARA CWSpar.savepar	/* If we use DOS memory for paging, amt to save */
#define MINPAGEDIR 4		/* May round down, 1PD, 2PT */
#define PAGE2PARA 256
#define KB2PARA 64

static word8 map[4096];		/* Expanded/Extended paged by valloc() */

static word16 mem_avail;
static word16 mem_used;		/* Pages, max 256Mb */

static unsigned pn_lo_first, pn_lo_last, pn_hi_first, pn_hi_last;
static unsigned pn_lo_next, pn_hi_next;
static char valloc_initted = 0;
static char use_vcpi = 0;

static int16 emb_handle=-1;

static void xms_free(void)
{
  if(use_xms && emb_handle != -1) {
    xms_unlock_emb(emb_handle);
    xms_emb_free(emb_handle);
    emb_handle = -1;
  }
}

static void xms_alloc_init(void)
{
  word32 linear_base;
  word16 emb_size;
  if((emb_size = xms_query_extended_memory()) != 0) {
    emb_handle = xms_emb_allocate(emb_size);
    linear_base = xms_lock_emb(emb_handle);
    pn_hi_first = (word16)((linear_base + 4095)/4096);
    pn_hi_last = (word16)((linear_base + emb_size * 1024L)/4096 - 1);
  } else {
    pn_hi_first = 1;
    pn_hi_last = 0;
  }
  SHOW_MEM_INFO("XMS memory: %ld Kb",(((word32)pn_hi_last-pn_hi_first+1) * 4));
}

static unsigned valloc_lowmem_page;
static unsigned lol;
static unsigned desired_pt;
static unsigned strategy, umbstat;
static unsigned mempid;
static unsigned oldmempid;

static void set_umb(void)
{
  oldmempid = get_pid();
  if(mempid) {
    if(oldmempid != mempid)
      set_pid(mempid);
  } else
    mempid = oldmempid;

  if (_osmajor >= 5) {
    _AX = 0x5800;
    geninterrupt(0x21);	/* Get allocation strategy */
    strategy = _AX;

    _AX = 0x5802;
    geninterrupt(0x21);	/* Get UMB status */
    umbstat = _AX;
    
    _AX = 0x5801;
    _BX = 0x0080;
    geninterrupt(0x21);	/* Set first fit high, then low */

    _AX = 0x5803;
    _BX = 0x0001;
    geninterrupt(0x21);	/* Include UMB in memory chain */
  }
}

static void restore_umb(void)
{
  if (_osmajor >= 5) {
    _AX = 0x5803;
    _BX = umbstat;
    _BH = 0;
    geninterrupt(0x21);	/* Restore memory chain UMB usage */

    _AX = 0x5801;
    _BX = strategy;
    geninterrupt(0x21);	/* Restore allocation stragegy */
  }
  if(oldmempid != mempid)
      set_pid(oldmempid);
}

static int alloc_pagetables(int mintable, int wanttable)
{
  set_umb();
  _AH = 0x48;		/* get real memory size */
  _BX = 0xffff;
  geninterrupt(0x21);	/* lol == size of largest free memory block */
  lol = _BX;

  if (lol < mintable*PAGE2PARA)	/* 1 PD, 1 PT (real), 1 PT (user) */
    goto mem_exit;

  if (lol > wanttable*PAGE2PARA) {	/* 8 will probably result in 5 user pt */
    if (mem_avail > MINAPPMEM)			/* 256K extended */
      lol = wanttable*PAGE2PARA;
    else {
      if (lol > wanttable*PAGE2PARA+SAVEPARA)
        lol -= SAVEPARA;			/* Reserve extra DOS memory */
      mem_avail += (lol >> 8) - wanttable;
    }
  }

  _BX = lol;
  _AH = 0x48;
  geninterrupt(0x21);		/* get the block */
  valloc_lowmem_page = _AX;
  if (_FLAGS & 1) {
mem_exit:
    restore_umb();
    return 1;
  }

  /* shrink memory to 4K align */
  if (valloc_lowmem_page & 0xFF) {
    lol -= (valloc_lowmem_page & 0xFF);
    _ES = valloc_lowmem_page;
    _BX = lol;
    _AH = 0x4A;
    geninterrupt(0x21);
  }
  restore_umb();

  pn_lo_first = (valloc_lowmem_page+0xFF) >> 8;	/* lowest real mem 4K block */
  pn_lo_last = (valloc_lowmem_page+lol-0x100)>>8;	/* highest real mem 4K block */

  pn_lo_next = pn_lo_first;
  return 0;
}

void valloc_init(void)
{
  unsigned i;

  if (valloc_initted)
    return;

  if (vcpi_installed) {
    pn_hi_first = 0;
    pn_hi_last  = vcpi_maxpage();
    i = vcpi_capacity();
    if (i) {
      use_vcpi = 1;
      SHOW_MEM_INFO("VCPI memory: %ld Kb", (i * 4L));
    } else if(use_xms) {
      use_vcpi = 0;	/* Just in case multiple pass with all allocated */
      xms_alloc_init();	/* Use XMS memory with VCPI mode switch */
    }
  } else if (use_xms) {
    xms_alloc_init();	/* Try XMS allocation */
    if (cpumode()) {
      errmsg("\nError: Using XMS switched the CPU into V86 mode.\n");
      xms_free();
      _exit(1);
    }
  } else if (mtype == PC98) { /* RAW memory not supported, 640K only */
    pn_hi_first = 256;
    pn_hi_last = 255;	/* Or set via memory size, how? */
  } else {
    /* int 15/vdisk memory allocation */
    /* Bug here - we should hook int 0x15 and reduce size, but who cares? */
    char has_vdisk=1;
    unsigned char far *vdisk;
    _AH = 0x88;		/* get extended memory size */
    geninterrupt(0x15);
    pn_hi_last = _AX / 4 + 255;

    /* get ivec 19h, seg only */
    vdisk = (unsigned char far *)(*(long far *)0x64L & 0xFFFF0000L);
    for (i=0; i<5; i++)
      if (vdisk[i+18] != "VDISK"[i])
        has_vdisk = 0;
    if (has_vdisk) {
      pn_hi_first = ( (vdisk[46]<<4) | (vdisk[45]>>4) );
      if (vdisk[44] | (vdisk[45]&0xf))
        pn_hi_first ++;
    }
    else
      pn_hi_first = 256;
    SHOW_MEM_INFO("Extended memory: %ld Kb", (((word32)pn_hi_last-pn_hi_first) * 4));
  }
  pn_hi_next = pn_hi_first;
  mem_avail = (use_vcpi)? vcpi_capacity():((long)pn_hi_last-pn_hi_first+1);
  
  if(NPAGEDIR)				/* Specified */
    desired_pt = 3 + NPAGEDIR;
  else {				/* Zero means automatic */
    desired_pt = 4 + (mem_avail>>10);	/* All physical mem plus 1 extra */
    if(desired_pt < 8)
      desired_pt = 8;
  }

  mempid = 0;
  if(alloc_pagetables(MINPAGEDIR, desired_pt)) {
    errmsg("Error: could not allocate page table memory\n");
    xms_free();
    _exit(1);
  }

  memset(map, 0, 4096);

  mem_used = 0;
  valloc_initted = 1;
  set_a20();
}

static void vset(unsigned i, char b)
{
  unsigned o;
  word8 m;
  o = (unsigned)(i>>3);
  m = 1<<((unsigned)i&7);
  if (b)
    map[o] |= m;
  else
    map[o] &= ~m;
}

static word8 vtest(unsigned i)
{
  unsigned o;
  word8 m;
  o = (unsigned)(i>>3);
  m = 1<<((unsigned)i&7);
  return map[o] & m;
}

static void vcpi_flush(void)		/* only called on exit */
{
  word16 pn;

  if (!use_vcpi)
    return;			/*  Not Initaialized Map[]  */
  for(pn = 0; pn <= pn_hi_last; pn++)
    if (vtest(pn))
      vcpi_free(pn);
}

void valloc_uninit(void)
{
  if (!valloc_initted)
    return;

  /* free the block we allocated - DOS does this
  _ES = valloc_lowmem_page;
  _AH = 0x49;
  geninterrupt(0x21); */

  xms_free();
  vcpi_flush();		/*  Deallocated VCPI Pages  */
  valloc_initted = 0;
  reset_a20();
}

unsigned valloc_640(void)
{
  unsigned pn;
  if (pn_lo_next <= pn_lo_last) {
    return pn_lo_last--;		/* Never deallocated! */
  }

  /* First try to resize current block instead of paging */
  {
    int failure;
    set_umb();
    lol += PAGE2PARA;
    _ES = valloc_lowmem_page;
    _BX = lol;
    _AH = 0x4A;
    geninterrupt(0x21);
    failure = _FLAGS & 1;
    restore_umb();
    if(!failure)
      return (valloc_lowmem_page+lol-0x100)>>8;
    /* Okay, not unexpected.  Allocate a new block, we can 
       hopefully resize it later if needed. */
    if(!alloc_pagetables(2,2))
      return pn_lo_last--;
  }

  pn = page_out_640();
  if (pn == 0xffff) {
    errmsg("Error: could not allocate page table memory\n");
    cleanup(1);
  }
  return pn;
}

unsigned valloc(void)
{
  unsigned pn;
  if (use_vcpi) {
    if ((pn = vcpi_alloc()) != 0) {
      mem_used++;
      vset(pn, 1);
      return pn;
    }
  } else {
    for (pn=pn_hi_next; pn<=pn_hi_last; pn++) 
      if (!vtest(pn)) {
        pn_hi_next = pn+1;
        mem_used++;
        vset(pn, 1);
        return pn;
      }
  }

  /* This section is only used if we are paging in 1Mb area; save for PDs */
  /* Note, if VCPI memory runs out before we get mem_avail also end up here */
  if (mem_used < mem_avail && pn_lo_next < 4+pn_lo_last-desired_pt) {
    mem_used++;
    return (word16)(vcpi_pt[pn_lo_next++] >> 12);
  }

  return page_out();
}

/* If we are able to find the page, return 1 */
int vfree(word16 pn)
{
  if (vtest(pn)) {
    vset(pn, 0);
    if (use_vcpi)
      vcpi_free(pn);
    else if(pn < pn_hi_next)
      pn_hi_next = pn;
    mem_used--;
    return 1;
  } 
  if (pn == vcpi_pt[pn_lo_next-1]) {
    pn_lo_next--;
    mem_used--;
    return 1;
  }
  return 0;
}

unsigned valloc_max_size(void)
{
  return (unsigned)(mem_avail);
}

unsigned valloc_used(void)
{
  return (unsigned)(mem_used);
}
