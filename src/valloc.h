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

/*
** These functions deal with page *numbers*
**  pn << 8  = segment number
**  pn << 12 = physical address
**  pn << 24 = seg:ofs
*/

void valloc_init(void);
void valloc_uninit(void);

unsigned valloc(void);
unsigned valloc_640(void);
int vfree(word16 pn);

void valloc_shrink_rmem(int pages);

/* These return *pages* not bytes, and don't get called in DPMI mode */
unsigned valloc_max_size(void);
unsigned valloc_used(void);
