#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include "stolen_chunk_of_pp_ctl.c"

#include "callchecker0.h"

STATIC OP*
install_sm_op(pTHX_ OP *o, GV *gv, SV *ud)
{
    OP *list, *left, *right, *new;

    list = cUNOPo->op_first;
    left = cLISTOPx(list)->op_first->op_sibling; /* skip over the pushmark */
    right = left->op_sibling;

    cLISTOPx(list)->op_first->op_sibling = right->op_sibling;
    left->op_sibling = right->op_sibling = NULL;
    op_free(o);

    new = newBINOP(OP_CUSTOM, 0, left, right);
    new->op_ppaddr = INT2PTR(Perl_ppaddr_t, Perl_pp_old_smartmatch);

    return new;
}

MODULE = smartmatch::engine::core  PACKAGE = smartmatch::engine::core

PROTOTYPES: DISABLE

void
init(match)
    SV *match;
    PREINIT:
    CV *cv;
    CODE:
        if (!SvROK(match) || SvTYPE(SvRV(match)) != SVt_PVCV) {
            croak("not a coderef");
        }

        cv = (CV*)SvRV(match);

        cv_set_call_checker(cv, install_sm_op, (SV*)cv);
