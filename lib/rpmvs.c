#include "system.h"

#include <rpm/rpmkeyring.h>
#include "lib/rpmvs.h"
#include "rpmio/digest.h"

#include "debug.h"

struct rpmvs_s {
    struct rpmsinfo_s *sigs;
    rpmRC *rcs;
    char **results;
    int nsigs;
};

static const struct rpmsinfo_s rpmvfyitems[] = {
    {	RPMSIGTAG_SIZE,			RPM_BIN_TYPE,		0,	0,
	RPMSIG_OTHER_TYPE,		0,
	(RPMSIG_HEADER|RPMSIG_PAYLOAD), 0, },
    {	RPMSIGTAG_PGP,			RPM_BIN_TYPE,		0,	0,
	RPMSIG_SIGNATURE_TYPE,		RPMVSF_NORSA,
	(RPMSIG_HEADER|RPMSIG_PAYLOAD), 0, },
    {	RPMSIGTAG_MD5,			RPM_BIN_TYPE,		0,	16,
	RPMSIG_DIGEST_TYPE,		RPMVSF_NOMD5,
	(RPMSIG_HEADER|RPMSIG_PAYLOAD), PGPHASHALGO_MD5, },
    {	RPMSIGTAG_GPG,			RPM_BIN_TYPE,		0,	0,
	RPMSIG_SIGNATURE_TYPE,		RPMVSF_NODSA,
	(RPMSIG_HEADER|RPMSIG_PAYLOAD), 0, },
    { 	RPMSIGTAG_PGP5,			RPM_BIN_TYPE,		0,	0,
	RPMSIG_SIGNATURE_TYPE,		RPMVSF_NORSA,
	(RPMSIG_HEADER|RPMSIG_PAYLOAD), 0, },
    {	RPMSIGTAG_PAYLOADSIZE,		RPM_INT32_TYPE,		1,	4,
	RPMSIG_OTHER_TYPE,		0,
	(RPMSIG_PAYLOAD),		0, },
    {	RPMSIGTAG_RESERVEDSPACE,	RPM_BIN_TYPE,		0,	0,
	RPMSIG_OTHER_TYPE,		0,
	0,				0, },
    {	RPMTAG_DSAHEADER,		RPM_BIN_TYPE,		0,	0,
	RPMSIG_SIGNATURE_TYPE,		RPMVSF_NODSAHEADER,
	(RPMSIG_HEADER),		0, },
    {	RPMTAG_RSAHEADER,		RPM_BIN_TYPE,		0,	0,
	RPMSIG_SIGNATURE_TYPE,		RPMVSF_NORSAHEADER,
	(RPMSIG_HEADER),		0, },
    {	RPMTAG_SHA1HEADER,		RPM_STRING_TYPE,	1,	41,
	RPMSIG_DIGEST_TYPE,		RPMVSF_NOSHA1HEADER,
	(RPMSIG_HEADER),		PGPHASHALGO_SHA1, },
    {	RPMSIGTAG_LONGSIZE,		RPM_INT64_TYPE,		1,	8,
	RPMSIG_OTHER_TYPE, 		0,
	(RPMSIG_HEADER|RPMSIG_PAYLOAD), 0, },
    {	RPMSIGTAG_LONGARCHIVESIZE,	RPM_INT64_TYPE,		1,	8,
	RPMSIG_OTHER_TYPE,		0,
	(RPMSIG_HEADER|RPMSIG_PAYLOAD),	0, },
    {	RPMTAG_SHA256HEADER,		RPM_STRING_TYPE,	1,	65,
	RPMSIG_DIGEST_TYPE,		RPMVSF_NOSHA256HEADER,
	(RPMSIG_HEADER),		PGPHASHALGO_SHA256, },
    {	RPMTAG_PAYLOADDIGEST,		RPM_STRING_ARRAY_TYPE,	0,	0,
	RPMSIG_DIGEST_TYPE,		RPMVSF_NOPAYLOAD,
	(RPMSIG_PAYLOAD),		PGPHASHALGO_SHA256, },
    { 0 } /* sentinel */
};

static int sinfoLookup(rpmTagVal tag, struct rpmsinfo_s *sinfo)
{
    const struct rpmsinfo_s *si;
    for (si = &rpmvfyitems[0]; si->tag; si++) {
	if (tag == si->tag) {
	    memcpy(sinfo, si, sizeof(*sinfo));
	    break;
	}
    }
    return (si->tag != tag);
}

rpmRC rpmsinfoInit(rpmtd td, const char *origin,
		      struct rpmsinfo_s *sinfo, char **msg)
{
    rpmRC rc = RPMRC_FAIL;
    const void *data = NULL;
    rpm_count_t dlen = 0;

    if (sinfoLookup(td->tag, sinfo)) {
	/* anything unknown just falls through for now */
	rc = RPMRC_OK;
	goto exit;
    }

    if (sinfo->tagtype && sinfo->tagtype != td->type) {
	rasprintf(msg, _("%s tag %u: BAD, invalid type %u"),
			origin, td->tag, td->type);
	goto exit;
    }

    if (sinfo->tagcount && sinfo->tagcount != td->count) {
	rasprintf(msg, _("%s: tag %u: BAD, invalid count %u"),
			origin, td->tag, td->count);
	goto exit;
    }

    switch (td->type) {
    case RPM_STRING_TYPE:
    case RPM_STRING_ARRAY_TYPE:
	data = rpmtdGetString(td);
	if (data)
	    dlen = strlen(data);
	break;
    case RPM_BIN_TYPE:
	data = td->data;
	dlen = td->count;
	break;
    }

    /* MD5 has data length of 16, everything else is (much) larger */
    if (sinfo->hashalgo && (data == NULL || dlen < 16)) {
	rasprintf(msg, _("%s tag %u: BAD, invalid data %p (%u)"),
			origin, td->tag, data, dlen);
	goto exit;
    }

    if (td->type == RPM_STRING_TYPE && td->size == 0)
	td->size = dlen + 1;

    if (sinfo->tagsize && (td->flags & RPMTD_IMMUTABLE) &&
		sinfo->tagsize != td->size) {
	rasprintf(msg, _("%s tag %u: BAD, invalid size %u"),
			origin, td->tag, td->size);
	goto exit;
    }

    if (sinfo->type == RPM_STRING_TYPE || sinfo->type == RPM_STRING_ARRAY_TYPE) {
	for (const char * b = data; *b != '\0'; b++) {
	    if (strchr("0123456789abcdefABCDEF", *b) == NULL) {
		rasprintf(msg, _("%s: tag %u: BAD, not hex"), origin, td->tag);
		goto exit;
	    }
	}
    }

    if (sinfo->type == RPMSIG_SIGNATURE_TYPE) {
	if (pgpPrtParams(data, dlen, PGPTAG_SIGNATURE, &sinfo->sig)) {
	    rasprintf(msg, _("%s tag %u: BAD, invalid OpenPGP signature"),
		    origin, td->tag);
	    goto exit;
	}
	sinfo->hashalgo = pgpDigParamsAlgo(sinfo->sig, PGPVAL_HASHALGO);
	sinfo->keyid = pgpGrab(sinfo->sig->signid+4, 4);
    } else if (sinfo->type == RPMSIG_DIGEST_TYPE) {
	if (td->type == RPM_BIN_TYPE)
	    sinfo->dig = pgpHexStr(data, dlen);
	else
	    sinfo->dig = xstrdup(data);
    }

    sinfo->tag = td->tag;
    if (sinfo->hashalgo)
	sinfo->id = td->tag;

    rc = RPMRC_OK;

exit:
    return rc;
}

void rpmsinfoFini(struct rpmsinfo_s *sinfo)
{
    if (sinfo) {
	if (sinfo->type == RPMSIG_SIGNATURE_TYPE)
	    pgpDigParamsFree(sinfo->sig);
	else if (sinfo->type == RPMSIG_DIGEST_TYPE)
	    free(sinfo->dig);
	memset(sinfo, 0, sizeof(*sinfo));
    }
}

int rpmsinfoDisabled(const struct rpmsinfo_s *sinfo, rpmVSFlags vsflags)
{
    if (!(sinfo->type & RPMSIG_VERIFIABLE_TYPE))
	return 1;
    if (vsflags & sinfo->disabler)
	return 1;
    if ((vsflags & RPMVSF_NEEDPAYLOAD) && (sinfo->range & RPMSIG_PAYLOAD))
	return 1;
    return 0;
}

void rpmvsAppend(struct rpmvs_s *sis, hdrblob blob, rpmTagVal tag)
{
    struct rpmtd_s td;
    sis->rcs[sis->nsigs] = hdrblobGet(blob, tag, &td);
    if (sis->rcs[sis->nsigs] == RPMRC_OK) {
	const char *o = _("package"); /* XXX not yet used for headers */
	int ix;
	while ((ix = rpmtdNext(&td)) >= -1) {
	    sis->rcs[sis->nsigs] = rpmsinfoInit(&td, o,
						&sis->sigs[sis->nsigs],
						&sis->results[sis->nsigs]);
	    sis->nsigs++;
	    break; /* XXX FIXME: handle realloc to support arrays */
	}
	rpmtdFreeData(&td);
    }
}

struct rpmvs_s *rpmvsCreate(hdrblob blob, rpmVSFlags vsflags)
{
    struct rpmvs_s *sis = xcalloc(1, sizeof(*sis));
    int nsigs = 1;
    for (const struct rpmsinfo_s *si = &rpmvfyitems[0]; si->tag; si++) {
	if (rpmsinfoDisabled(si, vsflags))
	    continue;
	nsigs++;
    }

    sis->sigs = xcalloc(nsigs, sizeof(*sis->sigs));
    sis->rcs = xcalloc(nsigs, sizeof(*sis->rcs));
    sis->results = xcalloc(nsigs, sizeof(*sis->results));

    for (const struct rpmsinfo_s *si = &rpmvfyitems[0]; si->tag; si++) {
	if (rpmsinfoDisabled(si, vsflags))
	    continue;
	rpmvsAppend(sis, blob, si->tag);
    }
    return sis;
}

struct rpmvs_s *rpmvsFree(struct rpmvs_s *sis)
{
    if (sis) {
	free(sis->rcs);
	for (int i = 0; i < sis->nsigs; i++) {
	    rpmsinfoFini(&sis->sigs[i]);
	    free(sis->results[i]);
	}
	free(sis->sigs);
	free(sis->results);
	free(sis);
    }
    return NULL;
}

void rpmvsInitDigests(struct rpmvs_s *sis, int range, rpmDigestBundle bundle)
{
    for (int i = 0; i < sis->nsigs; i++) {
	struct rpmsinfo_s *sinfo = &sis->sigs[i];
	if (sinfo->range & range) {
	    if (sis->rcs[i] == RPMRC_OK)
		rpmDigestBundleAddID(bundle, sinfo->hashalgo, sinfo->id, 0);
	}
    }
}

int rpmvsVerifyItems(struct rpmvs_s *sis, int range, rpmDigestBundle bundle,
		       rpmKeyring keyring, rpmsinfoCb cb, void *cbdata)
{
    int failed = 0;

    for (int i = 0; i < sis->nsigs; i++) {
	struct rpmsinfo_s *sinfo = &sis->sigs[i];

	if (sinfo->range == range) {
	    if (sis->rcs[i] == RPMRC_OK) {
		DIGEST_CTX ctx = rpmDigestBundleDupCtx(bundle, sinfo->id);
		sis->results[i] = _free(sis->results[i]);
		sis->rcs[i] = rpmVerifySignature(keyring, sinfo, ctx, &sis->results[i]);
		rpmDigestFinal(ctx, NULL, NULL, 0);
		rpmDigestBundleFinal(bundle, sinfo->id, NULL, NULL, 0);
	    }

	    if (cb)
		sis->rcs[i] = cb(sinfo, sis->rcs[i], sis->results[i], cbdata);

	    if (sis->rcs[i] != RPMRC_OK)
		failed++;
	}
    }

    return failed;
}

static const char * rpmSigString(rpmRC res)
{
    const char * str;
    switch (res) {
    case RPMRC_OK:		str = "OK";		break;
    case RPMRC_FAIL:		str = "BAD";		break;
    case RPMRC_NOKEY:		str = "NOKEY";		break;
    case RPMRC_NOTTRUSTED:	str = "NOTTRUSTED";	break;
    default:
    case RPMRC_NOTFOUND:	str = "UNKNOWN";	break;
    }
    return str;
}

static const char *rangeName(int range)
{
    switch (range) {
    case RPMSIG_HEADER:				return _("Header ");
    case RPMSIG_PAYLOAD:			return _("Payload ");
    }
    /* trad. output for (RPMSIG_HEADER|RPMSIG_PAYLOAD) range is "" */
    return "";
}

static rpmRC verifyDigest(struct rpmsinfo_s *sinfo, DIGEST_CTX digctx,
			  char **msg)
{
    rpmRC res = RPMRC_FAIL; /* assume failure */
    char * dig = NULL;
    size_t diglen = 0;
    DIGEST_CTX ctx = rpmDigestDup(digctx);
    char *title = rstrscat(NULL, rangeName(sinfo->range),
			   pgpValString(PGPVAL_HASHALGO, sinfo->hashalgo),
			   _(" digest:"), NULL);

    if (rpmDigestFinal(ctx, (void **)&dig, &diglen, 1) || diglen == 0) {
	rasprintf(msg, "%s %s", title, rpmSigString(res));
	goto exit;
    }

    if (strcasecmp(sinfo->dig, dig) == 0) {
	res = RPMRC_OK;
	rasprintf(msg, "%s %s (%s)", title, rpmSigString(res), sinfo->dig);
    } else {
	rasprintf(msg, "%s: %s Expected(%s) != (%s)",
		  title, rpmSigString(res), sinfo->dig, dig);
    }

exit:
    free(dig);
    free(title);
    return res;
}

/**
 * Verify DSA/RSA signature.
 * @param keyring	pubkey keyring
 * @param sinfo		OpenPGP signature parameters
 * @param hashctx	digest context
 * @retval msg		verbose success/failure text
 * @return 		RPMRC_OK on success
 */
static rpmRC
verifySignature(rpmKeyring keyring, struct rpmsinfo_s *sinfo,
		DIGEST_CTX hashctx, char **msg)
{
    rpmRC res = rpmKeyringVerifySig(keyring, sinfo->sig, hashctx);

    char *sigid = pgpIdentItem(sinfo->sig);
    rasprintf(msg, "%s%s: %s", rangeName(sinfo->range), sigid,
		rpmSigString(res));
    free(sigid);
    return res;
}

rpmRC
rpmVerifySignature(rpmKeyring keyring, struct rpmsinfo_s *sinfo,
		   DIGEST_CTX ctx, char ** result)
{
    rpmRC res = RPMRC_NOTFOUND;
    char *msg = NULL;

    if (sinfo->sig == NULL || ctx == NULL)
	goto exit;

    if (sinfo->type == RPMSIG_DIGEST_TYPE)
	res = verifyDigest(sinfo, ctx, &msg);
    else if (sinfo->type == RPMSIG_SIGNATURE_TYPE)
	res = verifySignature(keyring, sinfo, ctx, &msg);

exit:
    if (res == RPMRC_NOTFOUND) {
	rasprintf(&msg,
		  _("Verify signature: BAD PARAMETERS (%d %p %d %p)"),
		  sinfo->tag, sinfo->sig, sinfo->hashalgo, ctx);
	res = RPMRC_FAIL;
    }

    if (result) {
	*result = msg;
    } else {
	free(msg);
    }
    return res;
}
