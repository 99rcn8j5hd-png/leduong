# iOS build fix — validation status

Changes:
- Add proc_compat.hpp: Darwin ABI declarations plus calls to real runtime symbols via dlsym.
- No dependency on missing libproc.h / sys/proc_info.h headers in the iOS SDK.
- Missing runtime symbols return ENOSYS; no simulated success or kernel fallback.
- Require a full proc_bsdinfo response and use PROC_FLAG_TRACED=2 instead of 0x800.
- Link mach_reader.cpp in the macOS host target.
- Retire misleading syntax-apple fake-header validation.
- CI copies source to a path without spaces, tests host JSON, then compiles and links for iPhone.

Checked locally on Windows: archive contents, source references, ABI layout size/offset,
and workflow formatting. No local C++ compiler or Apple SDK is installed.
Host compilation and iOS compilation/linking MUST be confirmed by GitHub Actions.
Device execution, signing, permissions and real offsets remain unverified.
No automatic signing, jailbreak patching, or KernelReader implementation was added.

Sources:
https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/proc_info.h
https://github.com/apple-oss-distributions/xnu/blob/main/libsyscall/wrappers/libproc/libproc.h

Upload the contents of helper_interface_v2 into a NEW EMPTY repository root.
The .github/workflows/build-ios.yml file must be at the repository root.
Alternatively replace the old source folder with this patched folder and replace
the old repository-root workflow with the supplied workflow (do not duplicate it).
Run Actions > Build iPhone binaries > Run workflow.
