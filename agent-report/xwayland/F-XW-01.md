# F-XW-01 XWayland property parsing does not validate element format

- Agent confirmation: `confirmed`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: XWayland property parsing. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Three atom-list readers lack format validation and can overread replies; compositor crash reliability remains untested.

**Claim and verdict — confirmed.** The window-type, window-state, and WM-protocol property readers interpret the reply element count as a count of `xcb_atom_t` objects without checking its storage format. This is a source-confirmed out-of-bounds-read defect; the dossier's claim of an immediate, reliable compositor crash is not established.

**Source-confirmed — parsing contract.** `src/xwayland/XWM.cpp:217–218` obtains both the byte-length accessor and the data pointer, but the three handlers do not use that byte length. Installed protocol definition `/usr/share/xcb/xproto.xml:2267–2280` specifies a value byte span of `value_len * (format / 8)`. The distinct `format`, `type`, and `value_len` fields are declared at `/usr/include/xcb/xproto.h:1994–2003`. Atom width is fixed by `typedef uint32_t xcb_atom_t` at `/usr/include/xcb/xproto.h:104`, rather than by the reply's format. A property type atom supplies a semantic label, not a storage-width check.

**Source-confirmed — affected reads.** `src/xwayland/XWM.cpp:244–255` contains:

```cpp
auto* atomsArr = rc<const xcb_atom_t*>(value);
XSURF->m_atoms.assign(atomsArr, atomsArr + reply->value_len);
// In the separate state handler:
auto* atoms = rc<const xcb_atom_t*>(value);
for (uint32_t i = 0; i < reply->value_len; i++) {
    if (atoms[i] == HYPRATOMS["_NET_WM_STATE_MODAL"])
        XSURF->m_modal = true;
}
```

`src/xwayland/XWM.cpp:337–342` checks only the semantic type:

```cpp
if (reply->type != XCB_ATOM_ATOM)
    return;
auto* atoms = rc<const xcb_atom_t*>(value);
XSURF->m_protocols.assign(atoms, atoms + reply->value_len);
```

For a reply whose element storage is narrower than an atom, the requested source range exceeds the property's byte span. X11 reply padding does not generally supply the missing range. `std::vector::assign` copies the requested source range; it cannot infer the extent of an XCB allocation. The state loop has the same width/count mismatch without a vector copy.

**Source-confirmed — input boundary and caller guards.** The reviewed data flow is X11 window metadata received through the XWayland/XCB connection, followed by `CXWM`'s property fetch and `readProp`, then the three handlers above. The relevant ownership and dispatch checks are:

- `src/xwayland/XWM.cpp:47–65` resolves registered X window IDs and creates their surface records. `src/xwayland/XSurface.cpp:16–18` selects property-change events for those windows.
- `src/xwayland/XWM.cpp:370–387` rejects unknown windows and null replies, but requests `XCB_ATOM_ANY` and forwards non-null replies without checking their format or payload extent. Dispatch to the property handler is at `src/xwayland/XWM.cpp:895–904`; selection-event handling precedes it at `892–893`.
- The independent metadata refresh at `src/xwayland/XWM.cpp:1189–1203` includes all three properties, also requests `XCB_ATOM_ANY`, and checks only whether a reply exists. Its callers include the mapping path at `src/xwayland/XWM.cpp:110–136` and association at `1227–1230`.
- `src/xwayland/XWM.cpp:344–361` dispatches by property name. The `e->format == 32` guard at `438–439` belongs to a different client-message parser and does not protect property replies.

These are defensive source references to the trust boundary and missing guards, not an executed attack sequence. No crafted client, payload, or crash harness was used.

**Executed — read-only dependency inspection.** `pkg-config` identifies the installed libraries as XCB 1.17.0 and xcb-icccm 0.4.2. Inspection of `/usr/lib/libxcb.so` shows `xcb_get_property_value` merely returns the address after the reply header (symbol `0x12190`) and `xcb_get_property_value_length` computes the format-dependent byte count (symbol `0x121a0`). Neither accessor converts the data into an atom array. The command output is preserved in [dependency-inspection.txt](/home/wilf/data/.claude-scratch/agent-reports/work/05-xwayland/dependency-inspection.txt). This is dependency binary inspection, not execution of the vulnerable parser or evidence about every possible deployment's dependency build.

**Source-confirmed — reachability and permissions.** The precondition is an already-connected X11 client with a window tracked by this XWM, with permission to supply that window's metadata. Root access, compositor IPC, a special window rule, keyboard focus, and Wayland object-ID manipulation are not required by the reviewed property-reading paths. This assessment does not establish access for other users or unauthenticated peers. `xwayland:enabled` defaults to true at `src/config/values/ConfigValues.cpp:637`; startup is gated by the compile-time `NO_XWAYLAND` branch and by Xwayland availability/server creation at `src/xwayland/XWayland.cpp:6–39`. Builds or sessions with XWayland disabled do not expose this parser. `src/xwayland/XWM.cpp:1038` attaches XWM processing to the compositor event loop, so the unsafe reads run in the compositor process.

**Severity — High.** An untrusted local X11 metadata boundary reaches a memory-unsafe read in the compositor, with potential session-wide denial of service. This is an impact assessment based on source; no session crash was observed. Adjacent heap bytes can be copied into internal atom vectors or compared by the state loop, but this review establishes neither disclosure to a client, a write primitive, nor code execution. Downstream consumers are ordinary atom/trait comparisons: `src/xwayland/XSurface.cpp:175–193,289–305` and `src/desktop/view/window/X11Backend.cpp:71–95`. Their existence does not prove an additional exploitable consequence.

**Corrections to the dossier.** The affected handler line numbers still match this upstream baseline. However, a bounded GetProperty request limits returned bytes, not the consumer's interpretation of each element; the request's length parameter is in 32-bit units (`/usr/include/xcb/xproto.h:6906–6909`). The dossier's payload-size illustration is only one case, not a maximum overread bound. Reading beyond a heap allocation is undefined behavior and need not fault immediately; remove “mapping immediately crashes” and the unsupported frequency claim. Window type and modal traits are consumers; `m_role` is a separate string parsed at `src/xwayland/XWM.cpp:278–284`, so “role determination” is imprecise.

**Minimal fix direction.** Validate both the semantic ATOM type and 32-bit format before any of these three casts or ranges. Require the declared element count to fit the accessor's byte span, using division/comparison to avoid arithmetic overflow. Centralize this validation so all atom-list readers use the same contract. Add parser-level regression coverage for accepted atom lists, incompatible metadata, empty/missing properties, and truncated data in an isolated test environment; verify normal window state and protocol handling after the change. No source patch was made in this audit.

## Unnumbered control: WM_CLASS

**Claim and verdict — partially.** The dossier correctly identifies bounded string operations and the `!empty()` guard, but its explanation of what a final second NUL does is incorrect. This is an informational control record, not a compositor-crash finding.

**Source-confirmed — evidence and guard.** `src/xwayland/XWM.cpp:220–227` contains:

```cpp
XSURF->m_state.appid = std::string{value, valueLen};
if (std::count(XSURF->m_state.appid.begin(), XSURF->m_state.appid.end(), '\000') == 2)
    XSURF->m_state.appid = XSURF->m_state.appid.substr(XSURF->m_state.appid.find_first_of('\000') + 1);
if (!XSURF->m_state.appid.empty())
    XSURF->m_state.appid.pop_back();
```

The constructor uses the byte span from `src/xwayland/XWM.cpp:217–218`, rather than scanning for a terminator. When the count is exactly two, the first NUL exists and cannot be the final byte; thus `find_first_of + 1` is within the string. The substring contains the second NUL. If that second NUL is the final byte, the substring is still nonempty before `pop_back`; even an empty class consists of that one remaining NUL. Empty input takes neither the substring branch nor `pop_back`.

**Source-confirmed — data boundary and preconditions.** X11 class metadata reaches this branch through the property fetches described in F-XW-01 and dispatch at `src/xwayland/XWM.cpp:344–345`. The same enabled-XWayland and tracked-window conditions apply. No elevated permission or focus is required in these readers. The guard breaks the alleged empty-string mutation hazard.

**Corrections and severity.** The dossier's “second NUL at the end produces an empty substring” explanation is false, although its no-out-of-bounds conclusion holds here. There is a separate parsing limitation: `pop_back` removes the final byte of any nonempty result without checking that it is a terminator. Nonconforming strings can therefore be truncated. This review does not promote that metadata behavior into a memory-safety finding or a rule-bypass claim. Revised severity: **Informational**.

**Minimal fix direction.** Parse the instance/class separator explicitly, validate the expected terminators, and remove a trailing byte only when it is NUL. Preserve explicit input lengths. No patch or runtime check was performed for this record.

## Unnumbered control: WM_HINTS / size hints

**Claim and verdict — partially.** The conclusion that these two paths do not share F-XW-01's unchecked atom-range read is supported by the reviewed implementation and installed helpers. The claim that both have local excessive-`value_len` guards is wrong.

**Source-confirmed — WM_HINTS caller.** `src/xwayland/XWM.cpp:257–275` resets missing/empty hints, then validates the upper count and accepts only a successful ICCCM conversion:

```cpp
if (reply->value_len > XCB_ICCCM_NUM_WM_HINTS_ELEMENTS)
    return;
auto hints = makeUnique<xcb_icccm_wm_hints_t>();
if (!xcb_icccm_get_wm_hints_from_reply(hints.get(), reply))
    return;
XSURF->m_hints = std::move(hints);
```

The installed constant is nine (`/usr/include/xcb/xcb_icccm.h:788–789`). The caller reads the result's input flag only after successful conversion, at `src/xwayland/XWM.cpp:273–275`.

**Source-confirmed — size-hints caller.** `src/xwayland/XWM.cpp:302–316` handles missing/empty hints, checks the `WM_SIZE_HINTS` type, initializes the destination to zero, and checks the helper result:

```cpp
if (reply->type != HYPRATOMS["WM_SIZE_HINTS"])
    return;
auto sizeHints = makeUnique<xcb_size_hints_t>();
std::memset(sizeHints.get(), 0, sizeof(xcb_size_hints_t));
if (!xcb_icccm_get_wm_size_hints_from_reply(sizeHints.get(), reply))
    return;
```

There is **no local upper-count test** in this handler. The destination structure and its eighteen-element constant are defined at `/usr/include/xcb/xcb_icccm.h:545–569`. Subsequent field normalization occurs at `src/xwayland/XWM.cpp:318–334`.

**Executed — installed helper guard inspection.** Read-only disassembly saved in [dependency-inspection.txt](/home/wilf/data/.claude-scratch/agent-reports/work/05-xwayland/dependency-inspection.txt) establishes these properties of xcb-icccm 0.4.2:

- `xcb_icccm_get_wm_hints_from_reply`, symbol `0x2da0`, rejects null replies, the wrong WM_HINTS type, and formats other than 32 before copying. It requires at least eight elements. The caller's upper-count check bounds accepted data to nine elements; the helper also initializes the missing group field for the eight-element form.
- `xcb_icccm_get_wm_size_hints_from_reply`, symbol `0x2a40`, rejects null replies, the wrong WM_SIZE_HINTS type, and formats other than 32. Its copy is bounded to the smaller of the supplied count and eighteen elements. Shorter forms are handled within the zero-initialized destination; this does not establish strict rejection of every nonconforming ICCCM form.

These observations inspect the installed dependency, not a dependency commit pinned by Hyprland, and are not dynamic parser or compositor tests. Header declarations alone (`/usr/include/xcb/xcb_icccm.h:736–744,923–931`) would not establish these implementation guards.

**Source-confirmed — boundary, reachability, and severity.** The property readers and dispatch at `src/xwayland/XWM.cpp:352–353,358–359,1189–1203` accept client hints under the same XWayland conditions as F-XW-01. The checked helper conversions break the width/count-overread mechanism. No separate memory-safety defect is established in these two hint paths. Revised severity: **Informational**.

**Corrections and minimal fix direction.** Credit the dependency's type/format and bounded-copy checks rather than attributing all safety to a local count limit. The WM_HINTS line range in the dossier stops before the conversion guard; the size-hints range likewise omits the decisive helper call. No defect-specific patch is required for this control; direct caller format checks would make the dependency contract explicit, with regression coverage preserving accepted legacy forms.

## Unnumbered control: selection / WM_HINTS constant

**Claim and verdict — partially.** “No unchecked dereference seen” is supported only as a narrow statement about several null-pointer guards and the WM_HINTS helper path. It is not a valid general clearance of selection property parsing: the selection TARGETS atom reader also lacks storage-format validation.

**Source-confirmed — guarded portions.** Selection-event routing is at `src/xwayland/XWM.cpp:848–866`. `handleSelectionNotify` resolves the selection and rejects unknown ones at `661–669`; `getTransferData` checks whether a pending transfer exists and checks transfer/reply state at `1357–1379`. `SXTransfer::getIncomingSelectionProp` returns false for a missing reply at `1729–1741`. These are concrete pointer/state guards, not checks on each property's element representation. Separately, the `XCB_ICCCM_NUM_WM_HINTS_ELEMENTS` route is bounded and checked as documented in the preceding control section.

**Source-confirmed — limitation of the negative claim.** `src/xwayland/XDataSource.cpp:15–25` contains:

```cpp
if (!reply)
    return;
if (reply->type != XCB_ATOM_ATOM) {
    free(reply);
    return;
}
auto value = sc<xcb_atom_t*>(xcb_get_property_value(reply));
for (uint32_t i = 0; i < reply->value_len; i++) {
```

The null and type checks hold; neither establishes a 32-bit atom-list format or bounds the atom count by available bytes. Selection metadata enters `CXDataSource` through `setClipboardToWayland` at `src/xwayland/XWM.cpp:1332–1336`. That constructor parses before the empty-MIME check, so the latter is not an input-validation guard. This is the same category of missing representation check as F-XW-01. It overlaps the tracker entry **F-XW2-03**, which belongs to dossier 08; it is recorded here only to qualify dossier 05's control note, without adding a finding or auditing dossier 08.

**Source-confirmed — preconditions and limiting guard.** The relevant selection-notify branch requires a recognized selection and an X-focused surface (`src/xwayland/XWM.cpp:664–683`), in addition to XWayland being enabled and a client supplying selection metadata. That focus guard limits this selection path; it does not apply to F-XW-01's ordinary window-property readers. This review does not establish cross-user access or evaluate all selection authorization and transfer-lifetime behavior. As a positive comparison within the same directory, DnD proxy parsing explicitly validates reply presence, type, 32-bit format, and exact byte length at `src/xwayland/Dnd.cpp:43–61`.

**Severity, corrections, and minimal fix direction.** This record remains **Informational**, with an overlapping **High** memory-safety concern in selection atom-list parsing; it is not an additional independently counted High finding. Retain the observed pointer guards and WM_HINTS conclusion, but withdraw the broad implication that selection parsing is fully validated. Apply the shared atom-list validation direction from F-XW-01 to the selection reader as well. No selection request, malformed reply, or compositor test was executed.

## Completion and evidence limits

All dossier finding IDs and all three unnumbered control notes have been covered. Named finding counts: **1 confirmed, 0 partially, 0 rejected, 0 inconclusive**. Including the three control rows: **1 confirmed, 3 partially, 0 rejected, 0 inconclusive**.

The supplied example paths `reports/fork-fix-side-findings.md` and `reports/color-management-verification.md` were absent from this tracker checkout, so they were not used. The report follows BRIEF.md's requested structure directly. Only this report and the designated scratch dependency-inspection artifact were written. No source, dossier, README, other report, or Git state was modified by this review. No full build, exploit reproduction, live compositor, physical-display, or crash-reliability evidence is claimed.

[Full dossier-level verification report](_evidence/full-audit-05-xwayland.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/05-xwayland.md). These remain agent evidence, not human-confirmed material.
