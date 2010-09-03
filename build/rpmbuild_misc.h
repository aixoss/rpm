#ifndef _RPMBUILD_MISC_H
#define _RPMBUILD_MISC_H

#include <sys/types.h>
#include <rpm/rpmtypes.h>
#include <rpm/rpmds.h>

/** \ingroup rpmbuild
 * Destroy uid/gid caches.
 */
RPM_GNUC_INTERNAL
void freeNames(void);

/** \ingroup rpmbuild
 * Return cached user name from user id.
 * @todo Implement using hash.
 * @param uid		user id
 * @return		cached user name
 */
RPM_GNUC_INTERNAL
const char * getUname(uid_t uid);

/** \ingroup rpmbuild
 * Return cached user name.
 * @todo Implement using hash.
 * @param uname		user name
 * @return		cached user name
 */
RPM_GNUC_INTERNAL
const char * getUnameS(const char * uname);

/** \ingroup rpmbuild
 * Return cached user id.
 * @todo Implement using hash.
 * @param uname		user name
 * @return		cached uid
 */
RPM_GNUC_INTERNAL
uid_t getUidS(const char * uname);

/** \ingroup rpmbuild
 * Return cached group name from group id.
 * @todo Implement using hash.
 * @param gid		group id
 * @return		cached group name
 */
RPM_GNUC_INTERNAL
const char * getGname(gid_t gid);

/** \ingroup rpmbuild
 * Return cached group name.
 * @todo Implement using hash.
 * @param gname		group name
 * @return		cached group name
 */
RPM_GNUC_INTERNAL
const char * getGnameS(const char * gname);

/** \ingroup rpmbuild
 * Return cached group id.
 * @todo Implement using hash.
 * @param gname		group name
 * @return		cached gid
 */
RPM_GNUC_INTERNAL
gid_t getGidS(const char * gname);

/** \ingroup rpmbuild
 * Truncate comment lines.
 * @param s		skip white space, truncate line at '#'
 */
RPM_GNUC_INTERNAL
void handleComments(char * s);

/** \ingroup rpmbuild
 * Parse an unsigned number.
 * @param		line from spec file
 * @retval res		pointer to uint32_t
 * @return		0 on success, 1 on failure
 */
RPM_GNUC_INTERNAL
uint32_t parseUnsignedNum(const char * line, uint32_t * res);

/** \ingroup rpmbuild
 * Add dependency to header, filtering duplicates.
 * @param h		header
 * @param tagN		tag, identifies type of dependency
 * @param N		(e.g. Requires: foo < 0:1.2-3, "foo")
 * @param EVR		(e.g. Requires: foo < 0:1.2-3, "0:1.2-3")
 * @param Flags		(e.g. Requires: foo < 0:1.2-3, both "Requires:" and "<")
 * @param index		(0 always)
 * @return		0 on success, 1 on error
 */
RPM_GNUC_INTERNAL
int addReqProv(Header h, rpmTag tagN,
		const char * N, const char * EVR, rpmsenseFlags Flags,
		uint32_t index);

/** \ingroup rpmbuild
 * Add rpmlib feature dependency.
 * @param h		header
 * @param feature	rpm feature name (i.e. "rpmlib(Foo)" for feature Foo)
 * @param featureEVR	rpm feature epoch/version/release
 * @return		0 always
 */
RPM_GNUC_INTERNAL
int rpmlibNeedsFeature(Header h, const char * feature, const char * featureEVR);

#endif /* _RPMBUILD_MISC_H */