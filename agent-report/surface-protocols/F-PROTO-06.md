# F-PROTO-06 Toplevel parent traversal can overflow or use expired references

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Unbounded recursive descendant traversal can exhaust the stack; destructor unlinking defeats the claimed expired-child UAF path.

**Verdict: partially. Severity: High.**

The dossier combines an unbounded recursive parent-validation traversal with a speculative expired-weak-pointer/UAF scenario. Recursion is present and reachable; the suggested destruction-based UAF is not established.

**Evidence — source-confirmed.** Client `xdg_toplevel.set_parent` resolves a typed parent and calls `setNewParent` at `src/protocols/XDGShell.cpp:301-304`. That method rejects self-parenting (`:329-333`), then recursively explores the requesting toplevel's descendants (`:335-348`):

```cpp
for (const auto& c : tl->m_children) { // :337
    if (c == target)
        return true;
    any = any || exploreChildren(c, target); // :341
    if (any)
        break;
}
```

The traversal is not tail-recursive: it must combine the return value, test it, and continue iteration. There is no depth cap. The parent-child graph is populated at `:354-356`. Distinct live toplevels can form an arbitrarily deep acyclic tree without mapping buffers or requiring user input; validation of a node with deep descendants visits that subtree. Thus client-controlled depth reaches compositor-thread call-stack consumption even though graph cycles are rejected. Protocol ownership of the toplevels is strong (`src/protocols/XDGShell.hpp:300`), while the parent/children edges are weak (`:157,165`). A finite process stack cannot support unbounded recursive depth. Exact failure depth and actual SIGSEGV are not measured.

**Counterevidence — source-confirmed.** Parent changes remove the old child edge first (`XDGShell.cpp:324-327`), and destruction unlinks the child:

```cpp
if (m_parent)
    std::erase_if(m_parent->m_children,
        [this](const auto& other) { return !other || other.get() == this; }); // :365-366
```

Installed hyprutils distinguishes `expired()` from `valid()` during destruction (`WeakPtr.hpp:154-166`); weak boolean conversion uses `valid()` (`:188-190`), and weak dereference obtains the live raw pointer (`:221-226`). Self-weak pointers can therefore remain valid during destructor unlinking, even though `lock()` refuses destruction-in-progress. Merely observing a weak pointer without an explicit lock is insufficient to claim UAF. Sequential requests in one batch do not run concurrently with this traversal. No normal-lifecycle route retaining a dead child in `m_children` was found.

**Severity.** High for the source-closed local-client stack-exhaustion DoS, revised from the dossier's Medium under the brief's compositor-crash rubric. This is not a claim that a measured number of toplevels fits every machine's resource limits.

**Corrections/fix direction.** The dossier's deep-chain count is an estimate, not reproduced evidence, and its UAF escalation is withdrawn. Traverse iteratively with a visited/depth budget; prune expired edges defensively. Prefer bounded ancestor validation where it can preserve protocol semantics.

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
