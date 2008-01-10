/*
 * $Id$
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * Gallium Rubidium Oxygen Manganese Argon Carbon Silicon
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include "maths.h"
#include "macros.h"
#include "copyrite.h"
#include "bondf.h"
#include "string2.h"
#include "smalloc.h"
#include "strdb.h"
#include "sysstuff.h"
#include "confio.h"
#include "physics.h"
#include "statutil.h"
#include "vec.h"
#include "random.h"
#include "3dview.h"
#include "txtdump.h"
#include "readinp.h"
#include "names.h"
#include "toppush.h"
#include "pdb2top.h"
#include "gen_ad.h"
#include "topexcl.h"
#include "vec.h"
#include "atomprop.h"
#include "grompp.h"
#include "x2top_qgen.h"
#include "x2top_eemprops.h"

typedef struct {
  char *name;
  int  eemtype,elem,row;
  real J0,radius,chi0; 
  /* J0 in Yang & Sharp corresponds to n (eta) in Bultinck */
} t_eemprops;

typedef struct {
  int        nep;
  t_eemprops *eep;
} t_eemrecord;

static char *eemtype_name[eqgNR] = { 
  "None", "Linear", "Yang", "Bultinck", "SM" 
};

static int name2eemtype(char *name)
{
  int i;
  
  for(i=0; (i<eqgNR); i++) {
    if (strcasecmp(name,eemtype_name[i]) == 0)
      return i;
  }
  return -1;
}

void *read_eemprops(char *fn)
{
  t_eemrecord *eem=NULL;
  char   buf[STRLEN],**strings;
  int    i,n;
  char   nmbuf[32],algbuf[32];
  int    elem,row;
  double J0,radius,chi0;
  
  if (fn == NULL) 
    sprintf(buf,"eemprops.dat");
  else
    strcpy(buf,fn);
  n  = get_file(buf,&strings);
  if (n > 0) {
    snew(eem,1);
    snew(eem->eep,n);
    for(i=0; (i<n); i++) {
      if (sscanf(strings[i],"%s%s%d%d%lf%lf%lf",nmbuf,algbuf,&elem,&row,
		 &J0,&radius,&chi0) != 7) 
	gmx_fatal(FARGS,"Error in %s on line %d",buf,i+1);
      eem->eep[i].name    = strdup(nmbuf);
      if ((eem->eep[i].eemtype = name2eemtype(algbuf)) == -1)
	gmx_fatal(FARGS,"Error in %s on line %d, unknown algorithm '%s'",
		  buf,i+1,algbuf);
      eem->eep[i].elem    = elem;
      eem->eep[i].row     = row;
      eem->eep[i].J0      = J0;
      eem->eep[i].radius  = radius;
      eem->eep[i].chi0    = chi0;
    }
    eem->nep = n;
  }
  return eem;
}

void write_eemprops(FILE *fp,void *eem)
{
  t_eemrecord *er = (t_eemrecord *) eem;
  int i;
  
  for(i=0; (i<er->nep); i++)
    fprintf(fp,"%-5s  %10s  %3d  %3d  %10.3f  %10.3f  %10.3f\n",
	    er->eep[i].name,eemtype_name[er->eep[i].eemtype],
	    er->eep[i].elem,er->eep[i].row,er->eep[i].J0,
	    er->eep[i].radius,er->eep[i].chi0);
}

int eem_getnumprops(void *eem)
{
  t_eemrecord *er = (t_eemrecord *) eem;
  
  return er->nep;
}

int eem_getindex(void *eem,char *resname,char *aname,int eemtype)
{
  t_eemrecord *er = (t_eemrecord *) eem;
  int i;
  
  for(i=0; (i<er->nep); i++) 
    if ((strstr(aname,er->eep[i].name) == aname) && 
	(er->eep[i].eemtype == eemtype))
      return i;
  return -1;
}

real lo_get_j00(void *eem,int index,real *wj,real qH)
{
  t_eemrecord *er = (t_eemrecord *) eem;
  
  range_check(index,0,er->nep);

  if (er->eep[index].eemtype == eqgYang) {
    if (er->eep[index].elem == 1) 
      *wj = 10*(3/(4*er->eep[index].radius)+qH);
    else 
      *wj = 10*(3/(4*er->eep[index].radius));
  }
  else if (er->eep[index].eemtype == eqgSM)
    *wj = 10.0/er->eep[index].radius;
  else
    *wj = 0;
    
  return er->eep[index].J0;
}

real eem_get_j00(void *eem,char *resname,char *aname,real *wj,real qH,int eemtype)
{
  int k = eem_getindex(eem,resname,aname,eemtype);

  return lo_get_j00(eem,k,wj,qH);
}

real eem_get_chi0(void *eem,int index)
{
  t_eemrecord *er = (t_eemrecord *) eem;
  
  range_check(index,0,er->nep);
  
  return er->eep[index].chi0;
}

real eem_get_radius(void *eem,int index)
{
  t_eemrecord *er = (t_eemrecord *) eem;
  
  range_check(index,0,er->nep);
  
  return er->eep[index].radius;
}

void eem_set_props(void *eem,int index,real J0,real radius,real chi0)
{
  t_eemrecord *er = (t_eemrecord *) eem;
  
  range_check(index,0,er->nep);
  
  er->eep[index].J0 = J0;
  er->eep[index].radius = radius;
  er->eep[index].chi0 = chi0;
}