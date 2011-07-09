#define NEED_sv_2pv_flags
#include "ppport.h"

/* embed.h */

#define destroy_matcher(a)	S_destroy_matcher(aTHX_ a)
#define do_smartmatch(a,b)	S_do_smartmatch(aTHX_ a,b)
#define make_matcher(a)		S_make_matcher(aTHX_ a)
#define matcher_matches_sv(a,b)	S_matcher_matches_sv(aTHX_ a,b)

/* proto.h */

STATIC void	S_destroy_matcher(pTHX_ PMOP* matcher)
			__attribute__nonnull__(pTHX_1);
#define PERL_ARGS_ASSERT_DESTROY_MATCHER	\
	assert(matcher)

STATIC OP*	S_do_smartmatch(pTHX_ HV* seen_this, HV* seen_other);

STATIC PMOP*	S_make_matcher(pTHX_ REGEXP* re)
			__attribute__warn_unused_result__
			__attribute__nonnull__(pTHX_1);
#define PERL_ARGS_ASSERT_MAKE_MATCHER	\
	assert(re)

STATIC bool	S_matcher_matches_sv(pTHX_ PMOP* matcher, SV* sv)
			__attribute__warn_unused_result__
			__attribute__nonnull__(pTHX_1)
			__attribute__nonnull__(pTHX_2);
#define PERL_ARGS_ASSERT_MATCHER_MATCHES_SV	\
	assert(matcher); assert(sv)

/* pp_ctl.c */

STATIC PMOP *
S_make_matcher(pTHX_ REGEXP *re)
{
    dVAR;
    PMOP *matcher = (PMOP *) newPMOP(OP_MATCH, OPf_WANT_SCALAR | OPf_STACKED);

    PERL_ARGS_ASSERT_MAKE_MATCHER;

    PM_SETRE(matcher, ReREFCNT_inc(re));

    SAVEFREEOP((OP *) matcher);
    ENTER_with_name("matcher"); SAVETMPS;
    SAVEOP();
    return matcher;
}

STATIC bool
S_matcher_matches_sv(pTHX_ PMOP *matcher, SV *sv)
{
    dVAR;
    dSP;

    PERL_ARGS_ASSERT_MATCHER_MATCHES_SV;
    
    PL_op = (OP *) matcher;
    XPUSHs(sv);
    PUTBACK;
    (void) Perl_pp_match(aTHX);
    SPAGAIN;
    return (SvTRUEx(POPs));
}

STATIC void
S_destroy_matcher(pTHX_ PMOP *matcher)
{
    dVAR;

    PERL_ARGS_ASSERT_DESTROY_MATCHER;
    PERL_UNUSED_ARG(matcher);

    FREETMPS;
    LEAVE_with_name("matcher");
}

PP(pp_old_smartmatch)
{
    DEBUG_M(Perl_deb(aTHX_ "Starting smart match resolution\n"));
    return do_smartmatch(NULL, NULL);
}

STATIC OP *
S_do_smartmatch(pTHX_ HV *seen_this, HV *seen_other)
{
    dVAR;
    dSP;
    
    bool object_on_left = FALSE;
    SV *e = TOPs;	/* e is for 'expression' */
    SV *d = TOPm1s;	/* d is for 'default', as in PL_defgv */

    /* Take care only to invoke mg_get() once for each argument.
     * Currently we do this by copying the SV if it's magical. */
    if (d) {
	if (SvGMAGICAL(d))
	    d = sv_mortalcopy(d);
    }
    else
	d = &PL_sv_undef;

    assert(e);
    if (SvGMAGICAL(e))
	e = sv_mortalcopy(e);

    /* First of all, handle overload magic of the rightmost argument */
    if (SvAMAGIC(e)) {
	SV * tmpsv;
	DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-Object\n"));
	DEBUG_M(Perl_deb(aTHX_ "        attempting overload\n"));

	tmpsv = amagic_call(d, e, smart_amg, 0);
	if (tmpsv) {
	    SPAGAIN;
	    (void)POPs;
	    SETs(tmpsv);
	    RETURN;
	}
	DEBUG_M(Perl_deb(aTHX_ "        failed to run overload method; continuing...\n"));
    }

    SP -= 2;	/* Pop the values */


    /* ~~ undef */
    if (!SvOK(e)) {
	DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-undef\n"));
	if (SvOK(d))
	    RETPUSHNO;
	else
	    RETPUSHYES;
    }

    if (sv_isobject(e) && (SvTYPE(SvRV(e)) != SVt_REGEXP)) {
	DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-Object\n"));
	Perl_croak(aTHX_ "Smart matching a non-overloaded object breaks encapsulation");
    }
    if (sv_isobject(d) && (SvTYPE(SvRV(d)) != SVt_REGEXP))
	object_on_left = TRUE;

    /* ~~ sub */
    if (SvROK(e) && SvTYPE(SvRV(e)) == SVt_PVCV) {
	I32 c;
	if (object_on_left) {
	    goto sm_any_sub; /* Treat objects like scalars */
	}
	else if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVHV) {
	    /* Test sub truth for each key */
	    HE *he;
	    bool andedresults = TRUE;
	    HV *hv = (HV*) SvRV(d);
	    I32 numkeys = hv_iterinit(hv);
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Hash-CodeRef\n"));
	    if (numkeys == 0)
		RETPUSHYES;
	    while ( (he = hv_iternext(hv)) ) {
		DEBUG_M(Perl_deb(aTHX_ "        testing hash key...\n"));
		ENTER_with_name("smartmatch_hash_key_test");
		SAVETMPS;
		PUSHMARK(SP);
		PUSHs(hv_iterkeysv(he));
		PUTBACK;
		c = call_sv(e, G_SCALAR);
		SPAGAIN;
		if (c == 0)
		    andedresults = FALSE;
		else
		    andedresults = SvTRUEx(POPs) && andedresults;
		FREETMPS;
		LEAVE_with_name("smartmatch_hash_key_test");
	    }
	    if (andedresults)
		RETPUSHYES;
	    else
		RETPUSHNO;
	}
	else if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVAV) {
	    /* Test sub truth for each element */
	    I32 i;
	    bool andedresults = TRUE;
	    AV *av = (AV*) SvRV(d);
	    const I32 len = av_len(av);
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Array-CodeRef\n"));
	    if (len == -1)
		RETPUSHYES;
	    for (i = 0; i <= len; ++i) {
		SV * const * const svp = av_fetch(av, i, FALSE);
		DEBUG_M(Perl_deb(aTHX_ "        testing array element...\n"));
		ENTER_with_name("smartmatch_array_elem_test");
		SAVETMPS;
		PUSHMARK(SP);
		if (svp)
		    PUSHs(*svp);
		PUTBACK;
		c = call_sv(e, G_SCALAR);
		SPAGAIN;
		if (c == 0)
		    andedresults = FALSE;
		else
		    andedresults = SvTRUEx(POPs) && andedresults;
		FREETMPS;
		LEAVE_with_name("smartmatch_array_elem_test");
	    }
	    if (andedresults)
		RETPUSHYES;
	    else
		RETPUSHNO;
	}
	else {
	  sm_any_sub:
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-CodeRef\n"));
	    ENTER_with_name("smartmatch_coderef");
	    SAVETMPS;
	    PUSHMARK(SP);
	    PUSHs(d);
	    PUTBACK;
	    c = call_sv(e, G_SCALAR);
	    SPAGAIN;
	    if (c == 0)
		PUSHs(&PL_sv_no);
	    else if (SvTEMP(TOPs))
		SvREFCNT_inc_void(TOPs);
	    FREETMPS;
	    LEAVE_with_name("smartmatch_coderef");
	    RETURN;
	}
    }
    /* ~~ %hash */
    else if (SvROK(e) && SvTYPE(SvRV(e)) == SVt_PVHV) {
	if (object_on_left) {
	    goto sm_any_hash; /* Treat objects like scalars */
	}
	else if (!SvOK(d)) {
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-Hash ($a undef)\n"));
	    RETPUSHNO;
	}
	else if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVHV) {
	    /* Check that the key-sets are identical */
	    HE *he;
	    HV *other_hv = MUTABLE_HV(SvRV(d));
	    bool tied = FALSE;
	    bool other_tied = FALSE;
	    U32 this_key_count  = 0,
	        other_key_count = 0;
	    HV *hv = MUTABLE_HV(SvRV(e));

	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Hash-Hash\n"));
	    /* Tied hashes don't know how many keys they have. */
	    if (SvTIED_mg((SV*)hv, PERL_MAGIC_tied)) {
		tied = TRUE;
	    }
	    else if (SvTIED_mg((const SV *)other_hv, PERL_MAGIC_tied)) {
		HV * const temp = other_hv;
		other_hv = hv;
		hv = temp;
		tied = TRUE;
	    }
	    if (SvTIED_mg((const SV *)other_hv, PERL_MAGIC_tied))
		other_tied = TRUE;
	    
	    if (!tied && HvUSEDKEYS((const HV *) hv) != HvUSEDKEYS(other_hv))
	    	RETPUSHNO;

	    /* The hashes have the same number of keys, so it suffices
	       to check that one is a subset of the other. */
	    (void) hv_iterinit(hv);
	    while ( (he = hv_iternext(hv)) ) {
		SV *key = hv_iterkeysv(he);

		DEBUG_M(Perl_deb(aTHX_ "        comparing hash key...\n"));
	    	++ this_key_count;
	    	
	    	if(!hv_exists_ent(other_hv, key, 0)) {
	    	    (void) hv_iterinit(hv);	/* reset iterator */
		    RETPUSHNO;
	    	}
	    }
	    
	    if (other_tied) {
		(void) hv_iterinit(other_hv);
		while ( hv_iternext(other_hv) )
		    ++other_key_count;
	    }
	    else
		other_key_count = HvUSEDKEYS(other_hv);
	    
	    if (this_key_count != other_key_count)
		RETPUSHNO;
	    else
		RETPUSHYES;
	}
	else if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVAV) {
	    AV * const other_av = MUTABLE_AV(SvRV(d));
	    const I32 other_len = av_len(other_av) + 1;
	    I32 i;
	    HV *hv = MUTABLE_HV(SvRV(e));

	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Array-Hash\n"));
	    for (i = 0; i < other_len; ++i) {
		SV ** const svp = av_fetch(other_av, i, FALSE);
		DEBUG_M(Perl_deb(aTHX_ "        checking for key existence...\n"));
		if (svp) {	/* ??? When can this not happen? */
		    if (hv_exists_ent(hv, *svp, 0))
		        RETPUSHYES;
		}
	    }
	    RETPUSHNO;
	}
	else if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_REGEXP) {
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Regex-Hash\n"));
	  sm_regex_hash:
	    {
		PMOP * const matcher = make_matcher((REGEXP*) SvRV(d));
		HE *he;
		HV *hv = MUTABLE_HV(SvRV(e));

		(void) hv_iterinit(hv);
		while ( (he = hv_iternext(hv)) ) {
		    DEBUG_M(Perl_deb(aTHX_ "        testing key against pattern...\n"));
		    if (matcher_matches_sv(matcher, hv_iterkeysv(he))) {
			(void) hv_iterinit(hv);
			destroy_matcher(matcher);
			RETPUSHYES;
		    }
		}
		destroy_matcher(matcher);
		RETPUSHNO;
	    }
	}
	else {
	  sm_any_hash:
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-Hash\n"));
	    if (hv_exists_ent(MUTABLE_HV(SvRV(e)), d, 0))
		RETPUSHYES;
	    else
		RETPUSHNO;
	}
    }
    /* ~~ @array */
    else if (SvROK(e) && SvTYPE(SvRV(e)) == SVt_PVAV) {
	if (object_on_left) {
	    goto sm_any_array; /* Treat objects like scalars */
	}
	else if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVHV) {
	    AV * const other_av = MUTABLE_AV(SvRV(e));
	    const I32 other_len = av_len(other_av) + 1;
	    I32 i;

	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Hash-Array\n"));
	    for (i = 0; i < other_len; ++i) {
		SV ** const svp = av_fetch(other_av, i, FALSE);

		DEBUG_M(Perl_deb(aTHX_ "        testing for key existence...\n"));
		if (svp) {	/* ??? When can this not happen? */
		    if (hv_exists_ent(MUTABLE_HV(SvRV(d)), *svp, 0))
		        RETPUSHYES;
		}
	    }
	    RETPUSHNO;
	}
	if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVAV) {
	    AV *other_av = MUTABLE_AV(SvRV(d));
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Array-Array\n"));
	    if (av_len(MUTABLE_AV(SvRV(e))) != av_len(other_av))
		RETPUSHNO;
	    else {
	    	I32 i;
	    	const I32 other_len = av_len(other_av);

		if (NULL == seen_this) {
		    seen_this = newHV();
		    (void) sv_2mortal(MUTABLE_SV(seen_this));
		}
		if (NULL == seen_other) {
		    seen_other = newHV();
		    (void) sv_2mortal(MUTABLE_SV(seen_other));
		}
		for(i = 0; i <= other_len; ++i) {
		    SV * const * const this_elem = av_fetch(MUTABLE_AV(SvRV(e)), i, FALSE);
		    SV * const * const other_elem = av_fetch(other_av, i, FALSE);

		    if (!this_elem || !other_elem) {
			if ((this_elem && SvOK(*this_elem))
				|| (other_elem && SvOK(*other_elem)))
			    RETPUSHNO;
		    }
		    else if (hv_exists_ent(seen_this,
				sv_2mortal(newSViv(PTR2IV(*this_elem))), 0) ||
			    hv_exists_ent(seen_other,
				sv_2mortal(newSViv(PTR2IV(*other_elem))), 0))
		    {
			if (*this_elem != *other_elem)
			    RETPUSHNO;
		    }
		    else {
			(void)hv_store_ent(seen_this,
				sv_2mortal(newSViv(PTR2IV(*this_elem))),
				&PL_sv_undef, 0);
			(void)hv_store_ent(seen_other,
				sv_2mortal(newSViv(PTR2IV(*other_elem))),
				&PL_sv_undef, 0);
			PUSHs(*other_elem);
			PUSHs(*this_elem);
			
			PUTBACK;
			DEBUG_M(Perl_deb(aTHX_ "        recursively comparing array element...\n"));
			(void) do_smartmatch(seen_this, seen_other);
			SPAGAIN;
			DEBUG_M(Perl_deb(aTHX_ "        recursion finished\n"));
			
			if (!SvTRUEx(POPs))
			    RETPUSHNO;
		    }
		}
		RETPUSHYES;
	    }
	}
	else if (SvROK(d) && SvTYPE(SvRV(d)) == SVt_REGEXP) {
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Regex-Array\n"));
	  sm_regex_array:
	    {
		PMOP * const matcher = make_matcher((REGEXP*) SvRV(d));
		const I32 this_len = av_len(MUTABLE_AV(SvRV(e)));
		I32 i;

		for(i = 0; i <= this_len; ++i) {
		    SV * const * const svp = av_fetch(MUTABLE_AV(SvRV(e)), i, FALSE);
		    DEBUG_M(Perl_deb(aTHX_ "        testing element against pattern...\n"));
		    if (svp && matcher_matches_sv(matcher, *svp)) {
			destroy_matcher(matcher);
			RETPUSHYES;
		    }
		}
		destroy_matcher(matcher);
		RETPUSHNO;
	    }
	}
	else if (!SvOK(d)) {
	    /* undef ~~ array */
	    const I32 this_len = av_len(MUTABLE_AV(SvRV(e)));
	    I32 i;

	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Undef-Array\n"));
	    for (i = 0; i <= this_len; ++i) {
		SV * const * const svp = av_fetch(MUTABLE_AV(SvRV(e)), i, FALSE);
		DEBUG_M(Perl_deb(aTHX_ "        testing for undef element...\n"));
		if (!svp || !SvOK(*svp))
		    RETPUSHYES;
	    }
	    RETPUSHNO;
	}
	else {
	  sm_any_array:
	    {
		I32 i;
		const I32 this_len = av_len(MUTABLE_AV(SvRV(e)));

		DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-Array\n"));
		for (i = 0; i <= this_len; ++i) {
		    SV * const * const svp = av_fetch(MUTABLE_AV(SvRV(e)), i, FALSE);
		    if (!svp)
			continue;

		    PUSHs(d);
		    PUSHs(*svp);
		    PUTBACK;
		    /* infinite recursion isn't supposed to happen here */
		    DEBUG_M(Perl_deb(aTHX_ "        recursively testing array element...\n"));
		    (void) do_smartmatch(NULL, NULL);
		    SPAGAIN;
		    DEBUG_M(Perl_deb(aTHX_ "        recursion finished\n"));
		    if (SvTRUEx(POPs))
			RETPUSHYES;
		}
		RETPUSHNO;
	    }
	}
    }
    /* ~~ qr// */
    else if (SvROK(e) && SvTYPE(SvRV(e)) == SVt_REGEXP) {
	if (!object_on_left && SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVHV) {
	    SV *t = d; d = e; e = t;
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Hash-Regex\n"));
	    goto sm_regex_hash;
	}
	else if (!object_on_left && SvROK(d) && SvTYPE(SvRV(d)) == SVt_PVAV) {
	    SV *t = d; d = e; e = t;
	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Array-Regex\n"));
	    goto sm_regex_array;
	}
	else {
	    PMOP * const matcher = make_matcher((REGEXP*) SvRV(e));

	    DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-Regex\n"));
	    PUTBACK;
	    PUSHs(matcher_matches_sv(matcher, d)
		    ? &PL_sv_yes
		    : &PL_sv_no);
	    destroy_matcher(matcher);
	    RETURN;
	}
    }
    /* ~~ scalar */
    /* See if there is overload magic on left */
    else if (object_on_left && SvAMAGIC(d)) {
	SV *tmpsv;
	DEBUG_M(Perl_deb(aTHX_ "    applying rule Object-Any\n"));
	DEBUG_M(Perl_deb(aTHX_ "        attempting overload\n"));
	PUSHs(d); PUSHs(e);
	PUTBACK;
	tmpsv = amagic_call(d, e, smart_amg, AMGf_noright);
	if (tmpsv) {
	    SPAGAIN;
	    (void)POPs;
	    SETs(tmpsv);
	    RETURN;
	}
	SP -= 2;
	DEBUG_M(Perl_deb(aTHX_ "        failed to run overload method; falling back...\n"));
	goto sm_any_scalar;
    }
    else if (!SvOK(d)) {
	/* undef ~~ scalar ; we already know that the scalar is SvOK */
	DEBUG_M(Perl_deb(aTHX_ "    applying rule undef-Any\n"));
	RETPUSHNO;
    }
    else
  sm_any_scalar:
    if (SvNIOK(e) || (SvPOK(e) && looks_like_number(e) && SvNIOK(d))) {
	DEBUG_M(if (SvNIOK(e))
		    Perl_deb(aTHX_ "    applying rule Any-Num\n");
		else
		    Perl_deb(aTHX_ "    applying rule Num-numish\n");
	);
	/* numeric comparison */
	PUSHs(d); PUSHs(e);
	PUTBACK;
	if (CopHINTS_get(PL_curcop) & HINT_INTEGER)
	    (void) Perl_pp_i_eq(aTHX);
	else
	    (void) Perl_pp_eq(aTHX);
	SPAGAIN;
	if (SvTRUEx(POPs))
	    RETPUSHYES;
	else
	    RETPUSHNO;
    }
    
    /* As a last resort, use string comparison */
    DEBUG_M(Perl_deb(aTHX_ "    applying rule Any-Any\n"));
    PUSHs(d); PUSHs(e);
    PUTBACK;
    return Perl_pp_seq(aTHX);
}
