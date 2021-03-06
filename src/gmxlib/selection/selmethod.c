/*
 *
 *                This source code is part of
 *
 *                 G   R   O   M   A   C   S
 *
 *          GROningen MAchine for Chemical Simulations
 *
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2009, The GROMACS development team,
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
 */
/*! \file
 * \brief Implementation of functions in selmethod.h.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <string.h>

#include <macros.h>

#include <selmethod.h>

#include "selcollection.h"
#include "symrec.h"

/*
 * These global variables cannot be const because gmx_ana_selmethod_register()
 * modifies them to set some defaults. This is a small price to pay for the
 * convenience of not having to remember exactly how the selection compiler
 * expects the structures to be filled, and even more so if the expectations
 * change. Also, even if the gmx_ana_selmethod_t structures were made const,
 * the parameters could not be without typecasts somewhere, because the param
 * field in gmx_ana_selmethod_t cannot be declared const.
 *
 * Even though the variables may be modified, this should be thread-safe as
 * modifications are done only in gmx_ana_selmethod_register(), and it should
 * work even if called more than once for the same structure, and even if
 * called concurrently from multiple threads (as long as the selection
 * collection is not the same).
 *
 * All of these problems should go away if/when the selection methods are
 * implemented as C++ classes.
 */

/* From sm_com.c */
extern gmx_ana_selmethod_t sm_cog;
extern gmx_ana_selmethod_t sm_com;
/* From sm_simple.c */
extern gmx_ana_selmethod_t sm_all;
extern gmx_ana_selmethod_t sm_none;
extern gmx_ana_selmethod_t sm_atomnr;
extern gmx_ana_selmethod_t sm_resnr;
extern gmx_ana_selmethod_t sm_atomname;
extern gmx_ana_selmethod_t sm_atomtype;
extern gmx_ana_selmethod_t sm_resname;
extern gmx_ana_selmethod_t sm_insertcode;
extern gmx_ana_selmethod_t sm_chain;
extern gmx_ana_selmethod_t sm_mass;
extern gmx_ana_selmethod_t sm_charge;
extern gmx_ana_selmethod_t sm_altloc;
extern gmx_ana_selmethod_t sm_occupancy;
extern gmx_ana_selmethod_t sm_betafactor;
extern gmx_ana_selmethod_t sm_x;
extern gmx_ana_selmethod_t sm_y;
extern gmx_ana_selmethod_t sm_z;
/* From sm_distance.c */
extern gmx_ana_selmethod_t sm_distance;
extern gmx_ana_selmethod_t sm_mindistance;
extern gmx_ana_selmethod_t sm_within;
/* From sm_insolidangle.c */
extern gmx_ana_selmethod_t sm_insolidangle;

/* From sm_permute.c */
extern gmx_ana_selmethod_t sm_permute;

//! Array of selection methods defined in the library.
static gmx_ana_selmethod_t *const smtable_def[] = {
    &sm_cog,
    &sm_com,

    &sm_all,
    &sm_none,
    &sm_atomnr,
    &sm_resnr,
    &sm_atomname,
    &sm_atomtype,
    &sm_resname,
    &sm_insertcode,
    &sm_chain,
    &sm_mass,
    &sm_charge,
    &sm_altloc,
    &sm_occupancy,
    &sm_betafactor,
    &sm_x,
    &sm_y,
    &sm_z,

    &sm_distance,
    &sm_mindistance,
    &sm_within,
    &sm_insolidangle,

    &sm_permute,
};

/*! \brief
 * Convenience function for reporting errors found in selection methods.
 */
static void
report_error(FILE *fp, const char *name, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (fp)
    {
        fprintf(fp, "selection method '%s': ", name);
        vfprintf(fp, fmt, ap);
        fprintf(fp, "\n");
    }
    va_end(ap);
}

/*! \brief
 * Convenience function for reporting errors found in selection method parameters.
 */
static void
report_param_error(FILE *fp, const char *mname, const char *pname,
                   const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (fp)
    {
        fprintf(fp, "selection method '%s': parameter '%s': ", mname, pname);
        vfprintf(fp, fmt, ap);
        fprintf(fp, "\n");
    }
    va_end(ap);
}

/*! \brief
 * Checks the validity of parameters.
 *
 * \param[in]     fp      File handle to use for diagnostic messages
 *   (can be NULL).
 * \param[in]     name    Name of the method (used for error messages).
 * \param[in]     nparams Number of parameters in \p param.
 * \param[in,out] param   Parameter array
 *   (only the \c flags field of boolean parameters may be modified).
 * \param[in]     symtab  Symbol table (used for checking overlaps).
 * \returns       TRUE if there are no problems with the parameters,
 *   FALSE otherwise.
 *
 * This function performs some checks common to both check_method() and
 * check_modifier().
 * The purpose of these checks is to ensure that the selection parser does not
 * need to check for the validity of the parameters at each turn, and to
 * report programming errors as early as possible.
 * If you remove a check, make sure that the parameter parser can handle the
 * resulting parameters.
 */
static bool
check_params(FILE *fp, const char *name, int nparams, gmx_ana_selparam_t param[],
             gmx_sel_symtab_t *symtab)
{
    bool              bOk = TRUE;
    gmx_sel_symrec_t *sym;
    int               i, j;

    if (nparams > 0 && !param)
    {
        report_error(fp, name, "error: missing parameter data");
        bOk = FALSE;
        return FALSE;
    }
    if (nparams == 0 && param)
    {
        report_error(fp, name, "warning: parameter data unused because nparams=0");
    }
    /* Check each parameter */
    for (i = 0; i < nparams; ++i)
    {
        /* Check that there is at most one NULL name, in the beginning */
        if (param[i].name == NULL && i > 0)
        {
            report_error(fp, name, "error: NULL parameter should be the first one");
            bOk = FALSE;
            continue;
        }
        /* Check for duplicates */
        for (j = 0; j < i; ++j)
        {
            if (param[j].name == NULL)
            {
                continue;
            }
            if (!strcasecmp(param[i].name, param[j].name))
            {
                report_error(fp, name, "error: duplicate parameter name '%s'", param[i].name);
                bOk = FALSE;
                break;
            }
        }
        /* Check flags */
        if (param[i].flags & SPAR_SET)
        {
            report_param_error(fp, name, param[i].name, "warning: flag SPAR_SET is set");
            param[i].flags &= ~SPAR_SET;
        }
        if (param[i].flags & SPAR_RANGES)
        {
            if (param[i].val.type != INT_VALUE)
            {
                report_param_error(fp, name, param[i].name, "error: SPAR_RANGES cannot be set for a non-integer parameter");
                bOk = FALSE;
            }
            if (param[i].flags & SPAR_DYNAMIC)
            {
                report_param_error(fp, name, param[i].name, "warning: SPAR_DYNAMIC does not have effect with SPAR_RANGES");
                param[i].flags &= ~SPAR_DYNAMIC;
            }
            if (!(param[i].flags & SPAR_VARNUM) && param[i].val.nr != 1)
            {
                report_param_error(fp, name, param[i].name, "error: range should take either one or an arbitrary number of values");
                bOk = FALSE;
            }
            if (param[i].flags & SPAR_ATOMVAL)
            {
                report_param_error(fp, name, param[i].name, "error: SPAR_RANGES and SPAR_ATOMVAL both set");
                bOk = FALSE;
            }
        }
        if ((param[i].flags & SPAR_VARNUM) && (param[i].flags & SPAR_ATOMVAL))
        {
            report_param_error(fp, name, param[i].name, "error: SPAR_VARNUM and SPAR_ATOMVAL both set");
            bOk = FALSE;
        }
        /* Check boolean parameters */
        if (param[i].val.type == NO_VALUE)
        {
            if (param[i].val.nr != 0)
            {
                report_param_error(fp, name, param[i].name, "error: number of values should be zero for boolean parameters");
                bOk = FALSE;
            }
            /* The boolean parameters should always be optional, so set the
             * flag for convenience. */
            param[i].flags |= SPAR_OPTIONAL;
            /* Any other flags should not be specified */
            if (param[i].flags & ~SPAR_OPTIONAL)
            {
                report_param_error(fp, name, param[i].name, "error: boolean parameter should not have any flags set");
                bOk = FALSE;
            }
        }
        /* Check val.nr */
        if (param[i].flags & (SPAR_VARNUM | SPAR_ATOMVAL))
        {
            if (param[i].val.nr != -1)
            {
                report_param_error(fp, name, param[i].name, "warning: val.nr is not -1 although SPAR_VARNUM/SPAR_ATOMVAL is set");
            }
            param[i].val.nr = -1;
        }
        else if (param[i].val.type != NO_VALUE)
        {
            if (param[i].val.nr <= 0)
            {
                report_param_error(fp, name, param[i].name, "error: val.nr <= 0");
                bOk = FALSE;
            }
        }
        /* Check that the value pointer is NULL */
        if (param[i].nvalptr != NULL)
        {
            report_param_error(fp, name, param[i].name, "warning: nvalptr is set");
        }
        if (param[i].val.u.ptr != NULL)
        {
            report_param_error(fp, name, param[i].name, "warning: value pointer is set");
        }
        /* Check that the name contains only valid characters */
        if (param[i].name == NULL)
        {
            continue;
        }
        if (!isalpha(param[i].name[0]))
        {
            report_param_error(fp, name, param[i].name, "error: name does not begin with a letter");
            bOk = FALSE;
            continue;
        }
        for (j = 1; param[i].name[j] != 0; ++j)
        {
            if (param[i].name[j] != '_' && !isalnum(param[i].name[j]))
            {
                report_param_error(fp, name, param[i].name, "error: name contains non-alphanumeric characters");
                bOk = FALSE;
                break;
            }
        }
        if (param[i].name[j] != 0)
        {
            continue;
        }
        /* Check that the name does not conflict with a method */
        if (_gmx_sel_find_symbol(symtab, param[i].name, TRUE))
        {
            report_param_error(fp, name, param[i].name, "error: name conflicts with another method or a keyword");
            bOk = FALSE;
        }
    } /* End of parameter loop */
    /* Check parameters of existing methods */
    sym = _gmx_sel_first_symbol(symtab, SYMBOL_METHOD);
    while (sym)
    {
        gmx_ana_selmethod_t *method = _gmx_sel_sym_value_method(sym);
        gmx_ana_selparam_t  *param =
            gmx_ana_selmethod_find_param(name, method);
        if (param)
        {
            report_param_error(fp, method->name, param->name, "error: name conflicts with another method or a keyword");
            bOk = FALSE;
        }
        sym = _gmx_sel_next_symbol(sym, SYMBOL_METHOD);
    }
    return bOk;
}

/*! \brief
 * Checks the validity of selection method callback functions.
 *
 * \param[in] fp        File handle to use for diagnostic messages
 *   (can be NULL).
 * \param[in] method    The method to check.
 * \returns   TRUE if there are no problems, FALSE otherwise.
 *
 * This function performs some checks common to both check_method() and
 * check_modifier().
 * This function checks that all the required callbacks are defined, i.e.,
 * not NULL, to find programming errors.
 */
static bool
check_callbacks(FILE *fp, gmx_ana_selmethod_t *method)
{
    bool         bOk = TRUE;
    bool         bNeedInit;
    bool         bNeedFree;
    int          i;

    /* Make some checks on init_data and free */
    if (method->nparams > 0 && !method->init_data)
    {
        report_error(fp, method->name, "error: init_data should be provided because the method has parameters");
        bOk = FALSE;
    }
    if (method->free && !method->init_data)
    {
        report_error(fp, method->name, "warning: free is not used because of missing init_data");
    }
    /* Check presence of outinit for position-valued methods */
    if (method->type == POS_VALUE && !method->outinit)
    {
        report_error(fp, method->name, "error: outinit should be provided because the method has POS_VALUE");
        bOk = FALSE;
    }
    /* Warn of dynamic callbacks in static methods */
    if (!(method->flags & SMETH_MODIFIER))
    {
        if (method->init_frame && !(method->flags & SMETH_DYNAMIC))
        {
            report_error(fp, method->name, "warning: init_frame not used because the method is static");
        }
        if (method->pupdate && !(method->flags & SMETH_DYNAMIC))
        {
            report_error(fp, method->name, "warning: pupdate not used because the method is static");
            method->pupdate = NULL;
        }
    }
    /* Check that there is an evaluation function */
    if (method->type != NO_VALUE && !method->update && !method->pupdate)
    {
        report_error(fp, method->name, "error: evaluation function missing");
        bOk = FALSE;
    }
    /* Loop through the parameters to determine if initialization callbacks
     * are needed. */
    bNeedInit = FALSE;
    bNeedFree = FALSE;
    for (i = 0; i < method->nparams; ++i)
    {
        if (method->param[i].val.type == POS_VALUE
            || method->param[i].val.type == GROUP_VALUE)
        {
            bNeedFree = TRUE;
        }
        if (method->param[i].val.type != POS_VALUE
            && (method->param[i].flags & (SPAR_VARNUM | SPAR_ATOMVAL)))
        {
            bNeedInit = TRUE;
            bNeedFree = TRUE;
        }
    }
    /* Check that the callbacks required by the parameters are present */
    if (bNeedInit && !method->init)
    {
        report_error(fp, method->name, "error: init should be provided");
        bOk = FALSE;
    }
    if (bNeedFree && !method->free)
    {
        report_error(fp, method->name, "error: free should be provided");
        bOk = FALSE;
    }
    return bOk;
}

/*!
 * Checks the validity of a selection method.
 *
 * \param[in]     fp     File handle to use for diagnostic messages
 *   (can be NULL).
 * \param[in,out] method Method to check.
 * \param[in]     symtab Symbol table (used for checking overlaps).
 *
 * Checks the validity of the given selection method data structure
 * that does not have \ref SMETH_MODIFIER set.
 * If you remove a check, please make sure that the selection parser,
 * compiler, and evaluation functions can deal with the method.
 */
static bool
check_method(FILE *fp, gmx_ana_selmethod_t *method, gmx_sel_symtab_t *symtab)
{
    bool         bOk = TRUE;

    /* Check the type */
    if (method->type == NO_VALUE)
    {
        report_error(fp, method->name, "error: no value type specified");
        bOk = FALSE;
    }
    if (method->type == STR_VALUE && method->nparams > 0)
    {
        report_error(fp, method->name, "error: evaluates to a string but is not a keyword");
        bOk = FALSE;
    }
    /* Check flags */
    if (method->type == GROUP_VALUE)
    {
        /* Group methods should always have SMETH_SINGLEVAL,
         * so set it for convenience. */
        method->flags |= SMETH_SINGLEVAL;
        /* Check that conflicting flags are not present. */
        if (method->flags & SMETH_VARNUMVAL)
        {
            report_error(fp, method->name, "error: SMETH_VARNUMVAL cannot be set for group-valued methods");
            bOk = FALSE;
        }
    }
    else
    {
        if ((method->flags & SMETH_SINGLEVAL)
            && (method->flags & SMETH_VARNUMVAL))
        {
            report_error(fp, method->name, "error: SMETH_SINGLEVAL and SMETH_VARNUMVAL both set");
            bOk = FALSE;
        }
    }
    if ((method->flags & SMETH_CHARVAL) && method->type != STR_VALUE)
    {
        report_error(fp, method->name, "error: SMETH_CHARVAL can only be specified for STR_VALUE methods");
        bOk = FALSE;
    }
    /* Check the parameters */
    if (!check_params(fp, method->name, method->nparams, method->param, symtab))
    {
        bOk = FALSE;
    }
    /* Check the callback pointers */
    if (!check_callbacks(fp, method))
    {
        bOk = FALSE;
    }

    return bOk;
}

/*!
 * Checks the validity of a selection modifier method.
 *
 * \param[in]     fp     File handle to use for diagnostic messages
 *   (can be NULL).
 * \param[in,out] method Method to check.
 * \param[in]     symtab Symbol table (used for checking overlaps).
 *
 * Checks the validity of the given selection method data structure
 * that has \ref SMETH_MODIFIER set.
 * If you remove a check, please make sure that the selection parser,
 * compiler, and evaluation functions can deal with the method.
 */
static bool
check_modifier(FILE *fp, gmx_ana_selmethod_t *method, gmx_sel_symtab_t *symtab)
{
    bool         bOk = TRUE;

    /* Check the type */
    if (method->type != NO_VALUE && method->type != POS_VALUE)
    {
        report_error(fp, method->name, "error: modifier should have type POS_VALUE or NO_VALUE");
        bOk = FALSE;
    }
    /* Check flags */
    if (method->flags & (SMETH_SINGLEVAL | SMETH_VARNUMVAL))
    {
        report_error(fp, method->name, "error: modifier should not have SMETH_SINGLEVAL or SMETH_VARNUMVAL set");
        bOk = FALSE;
    }
    /* Check the parameters */
    /* The first parameter is skipped */
    if (!check_params(fp, method->name, method->nparams-1, method->param+1, symtab))
    {
        bOk = FALSE;
    }
    /* Check the callback pointers */
    if (!check_callbacks(fp, method))
    {
        bOk = FALSE;
    }
    if (method->update)
    {
        report_error(fp, method->name, "error: modifier should not have update");
        bOk = FALSE;
    }
    if (method->type == POS_VALUE && !method->pupdate)
    {
        report_error(fp, method->name, "error: evaluation function missing");
        bOk = FALSE;
    }

    return bOk;
}

/*!
 * \param[in,out] sc     Selection collection to registered the method to.
 * \param[in]     name   Name under which the method should be registered.
 * \param[in]     method Method to register.
 * \returns       0 on success, EINVAL if there was something wrong with the
 *   method.
 *
 * \p name does not need to match the name of the method, and the same
 * method can be registered multiple times under different names.
 * If \p name equals some previously registered name,
 * an error message is printed and the method is not registered.
 *
 * The function also performs some sanity checking on the input method,
 * and refuses to register it if there are problems.
 * Some problems only generate warnings.
 * All problems are described to \p stderr.
 */
int
gmx_ana_selmethod_register(struct gmx_ana_selcollection_t *sc,
                           const char *name, gmx_ana_selmethod_t *method)
{
    bool bOk;

    /* Check the method */
    if (method->flags & SMETH_MODIFIER)
    {
        bOk = check_modifier(stderr, method, sc->symtab);
    }
    else
    {
        bOk = check_method(stderr, method, sc->symtab);
    }
    /* Try to register the method if everything is ok */
    if (bOk) 
    {
        if (!_gmx_sel_add_method_symbol(sc->symtab, name, method))
        {
            bOk = FALSE;
        }
    }
    if (!bOk)
    {
        report_error(stderr, name, "warning: not registered");
        return EINVAL;
    }
    return 0;
}

/*!
 * \param[in,out] sc     Selection collection to registered the methods to.
 * \returns       0 on success, -1 if any of the default methods could not be
 *   registered.
 */
int
gmx_ana_selmethod_register_defaults(struct gmx_ana_selcollection_t *sc)
{
    size_t i;
    int  rc;
    bool bOk;

    bOk = TRUE;
    for (i = 0; i < asize(smtable_def); ++i)
    {
        rc = gmx_ana_selmethod_register(sc, smtable_def[i]->name, smtable_def[i]);
        if (rc != 0)
        {
            bOk = FALSE;
        }
    }
    return bOk ? 0 : -1;
}

/*!
 * \param[in] name   Name of the parameter to search.
 * \param[in] method Method to search for the parameter.
 * \returns   Pointer to the parameter in the
 *   \ref gmx_ana_selmethod_t::param "method->param" array,
 *   or NULL if no parameter with name \p name was found.
 *
 * This is a simple wrapper for gmx_ana_selparam_find().
 */
gmx_ana_selparam_t *
gmx_ana_selmethod_find_param(const char *name, gmx_ana_selmethod_t *method)
{
    return gmx_ana_selparam_find(name, method->nparams, method->param);
}
