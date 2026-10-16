# F-PROTO-05 Unchecked layer-shell geometry arithmetic

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: Shell / surface protocols. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Margin sums use doubles, rejecting the cited overflow site; applyExclusive converts margins to integers and performs unchecked signed addition.

**Verdict: partially. Severity: Medium.**

The dossier alleges signed overflow in layer margin sums and shared-layout corruption. The cited margin sums are floating point, but a separate signed addition in exclusive reservation is vulnerable to overflow.

**Evidence — source-confirmed.** Client setters store exclusive zone and margins at `src/protocols/LayerShell.cpp:100-107`. The actual state types are:

```cpp
int32_t exclusive = 0;                         // LayerShell.hpp:56
// ...
double left = 0, right = 0, top = 0, bottom = 0; // :64
```

Therefore the sums at `src/render/Renderer.cpp:2863,2873` are `double`, as are the box coordinates and sizes; the dossier's alleged `int32_t` overflow there is rejected. The size guard at `:2881-2884` skips nonpositive boxes.

**Evidence — source-confirmed.** The remaining path is layer request → `markPending` (`LayerShell.cpp:182-185`) → core-surface commit → layer state application (`:43-65,82`) → desktop layer commit (`src/desktop/view/LayerSurface.cpp:300-308,325-354`) → renderer arrangement (`Renderer.cpp:2813-2829,2888-2890`) → `applyExclusive`. That helper accepts `int32_t` margins (`:2755-2756`), stores each in `int margin` (`:2765-2797`), and evaluates:

```cpp
if ((exclusiveEdge == edges[i].singular_anchor ||
     anchor == edges[i].singular_anchor || anchor == edges[i].anchor_triplet)
    && exclusive + edges[i].margin > 0) {       // :2801
    *edges[i].positive_axis += exclusive + edges[i].margin; // :2803
    // negative axis similarly at :2806
}
```

Margins originate as representable `int32_t` values, so their conversion back from double is not itself the fault. Positive exclusive zone plus a sufficiently large positive matching-edge margin can exceed signed-int range. This expression is evaluated before the resulting amount is added to double geometry; floating-point destinations do not prevent that overflow. Positive desired width/height and a singular edge can satisfy the prior size and anchor checks. No buffer mapping is required for arrangement of an unconfigured layer; an available monitor and access to layer-shell are required. The global is normally visible to unsandboxed clients but filtered for security-context sandboxed clients (`src/Compositor.cpp:267-271`, `src/managers/ProtocolManager.cpp:348-403`); the dossier's arbitrary-client wording should not erase this distinction.

**Severity.** Medium for reachable UB and unchecked reservation arithmetic affecting shared usable area, not a proven crash or memory corruption. Even non-overflowing excessive positive reservations can shrink usable area below zero (`:2806`); the resulting area is used for subsequent layers and recorded at `:2916-2927`. Layer-shell intentionally grants layout authority, so ordinary large exclusive zones alone are not evidence of a privilege boundary violation.

**Corrections/fix direction.** The dossier's `TOP|LEFT` example is not a matching singular edge or supported anchor triplet without an explicit exclusive edge, so it does not establish this overflow branch. Negative/oversized `set_size` values are not categorically rejected: `LayerShell.cpp:57-64` permits nonpositive sizes when both opposite anchors are present; arrangement may subsequently skip them. Use wide arithmetic for the exclusive-plus-margin sum and bound the resulting reservation to sensible output geometry, preserving legitimate negative-margin semantics. No UBSan or compositor execution was performed.

[Full dossier-level verification report](_evidence/full-audit-06-protocols-shell.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/06-protocols-shell.md). These remain agent evidence, not human-confirmed material.
