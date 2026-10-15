# subsurface-orphan-parent — client for the SIDE-01 crash

Triggers the SIGSEGV in `CWLSubsurfaceResource::t1Parent()` (`Subcompositor.cpp:145`):
`wl_subcompositor.get_subsurface` is given a parent that is itself a subsurface
whose parent `wl_surface` was already destroyed, so `t1Parent()` dereferences a
null `m_parent.lock()`.

**This takes down the compositor it connects to.** Run it only in a nested or
throwaway session.

Build with `bash build.sh`, then run `./repro`. Analysis, verification and the
fix: [`../../_evidence/fork-fix-side-findings.md`](../../_evidence/fork-fix-side-findings.md#side-01).
