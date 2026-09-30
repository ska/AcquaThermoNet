# Version from the latest reachable git tag ("v2.0.0" -> "2.0.0"), so a
# release is just `git tag -a vX.Y.Z -m vX.Y.Z`, no source edit/commit.
# Built exactly at the tag: "2.0.0"; commits after it: the short hash of
# the commit is appended, "2.0.0-0566e34".
# Falls back to 0.0.0 without git, outside a checkout or with no tag
# reachable (e.g. a shallow clone without tags).
# Taken when qmake runs, like the git commit and the build time: rerun
# qmake after a commit or a tag (Qt Creator: Build > Run qmake).
# Generates version.h in the build dir from version.h.in (there \" is a
# quote: qmake drops plain quotes in QMAKE_SUBSTITUTES).
# tools/package/make_package.sh computes the same version.

ATN_GIT_HASH = $$system(git -C $$ATN_SRC rev-parse --short HEAD 2>/dev/null)

ATN_VERSION = 0.0.0
ATN_GIT_TAG = $$system(git -C $$ATN_SRC describe --tags --abbrev=0 2>/dev/null)
ATN_GIT_TAG ~= s/^v//
contains(ATN_GIT_TAG, "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?(\\.[0-9]+)?$") {
    ATN_VERSION = $$ATN_GIT_TAG
} else:!isEmpty(ATN_GIT_TAG) {
    warning("latest git tag '$$ATN_GIT_TAG' is not a plain X.Y.Z version, using $$ATN_VERSION")
}
!isEmpty(ATN_GIT_HASH):!system(git -C $$ATN_SRC describe --tags --exact-match HEAD >/dev/null 2>&1): \
    ATN_VERSION = $${ATN_VERSION}-$${ATN_GIT_HASH}

# Short hash of the commit, "-dirty" with uncommitted changes
isEmpty(ATN_GIT_HASH) {
    ATN_GIT_COMMIT = unknown
} else {
    ATN_GIT_COMMIT = $$ATN_GIT_HASH
    !system(git -C $$ATN_SRC diff-index --quiet HEAD --): ATN_GIT_COMMIT = $${ATN_GIT_COMMIT}-dirty
}

ATN_BUILD_TIMESTAMP = $$system(date -u \"+%Y-%m-%d %H:%M:%S UTC\")

atn_version.input  = $$ATN_SRC/version.h.in
atn_version.output = $$OUT_PWD/version.h
QMAKE_SUBSTITUTES += atn_version
INCLUDEPATH += $$OUT_PWD
