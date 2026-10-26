#include "BuildCommit.hpp"
#include "git_commit_hash.h"
#include "libslic3r_version.h"

namespace Slic3r { namespace GUI {

const char *const build_commit_hash  = GIT_COMMIT_HASH;
const char *const build_commit_label = GIT_COMMIT_HASH GIT_COMMIT_SUFFIX;
const char *const build_fork_label   = sizeof(BAMBULOCAL_PR) > 1 ? "BambuLocal V" BambuLocal_VERSION " (PR #" BAMBULOCAL_PR ")"
                                                                 : "BambuLocal V" BambuLocal_VERSION;

}} // namespace Slic3r::GUI
