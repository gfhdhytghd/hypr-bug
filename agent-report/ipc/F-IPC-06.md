# F-IPC-06 hyprctl batch delimiters are not escaped

- Agent confirmation: `partially`.
- Human confirmation: `none`; no human review or test record is supplied for this tracker.
- Scope: upstream source review at `e2ad066c9`; current-version and live acceptance must be established separately.

## Agent report

Area: IPC / hyprctl. Baseline: official Hyprland `main` at `e2ad066c9`.

Tracker note: Batch text is intentionally raw; ordinary dispatch does not split semicolons. Injection requires external data inserted into explicit batch syntax.

**Claim.** Unescaped `;` and backslash in hyprctl arguments let ordinary dispatch arguments become extra IPC commands, with broader script-dependent injection through batch mode.

**Verdict: partially.** Explicit batch text has a delimiter/escaping contract; the ordinary single-request example does not activate that parser.

**Evidence — source-confirmed.** `hyprctl/src/main.cpp:478,486-488` joins arguments with spaces, removes the trailing space, and adds the flag prefix. `main.cpp:433-434,532-533` selects explicit batch mode; `347-356` takes the supplied batch command text and prepends `[[BATCH]]`. It deliberately preserves batch syntax. `hyprctl/src/Strings.hpp:69` documents `--batch` as executing semicolon-separated commands.

The server's gate is decisive (`src/ipc/s1/S1.cpp:205-210`):

```cpp
if (request.starts_with(BATCH_TOKEN))
    return dispatchBatch(std::move(request), pid);
return dispatchSingle(std::move(request), pid);
```

Only `dispatchBatch` splits on bare semicolons (`S1.cpp:135-141`), and it consumes the supported backslash escapes (`144-150`) before calling `dispatchSingle` for each piece (`156-158`). The normal parser/dispatcher does not split semicolons. A syntax error or Lua behavior in an ordinary dispatch argument is a separate F-IPC-02 question, not proof of batch delimiter injection.

**Full conditional chain / reachability — source-confirmed.** An external string can affect extra commands if a trusted caller places it, without batch escaping, inside text intentionally sent through `--batch` or the batch protocol token. The client forwards that text, the server selects `dispatchBatch`, the tokenizer identifies delimiters, and it dispatches the resulting command pieces. That chain requires the trusted wrapper/data insertion precondition; no affected wrapper from the repository or a third-party tool was supplied. Shell quoting preserves an argument across the shell but does not escape this separate batch grammar. The resulting commands retain the peer's ordinary socket authority, including plugin permission checks where enabled.

**Offline check — executed.** An extracted-method harness under the assigned scratch directory compiled with `g++ -std=c++23 -Wall -Wextra -Werror` and passed 10 benign parser/tokenizer checks; see `parsing-results.txt`, `parsing_check.cpp`, `check_parsing.py`, and `extraction.txt`. Exact upstream `parseRequest` and the batch tokenization body were used; logging was suppressed and ASCII trimming was stubbed. Checks include preservation of a semicolon in a normal parsed argument, splitting two diagnostic names in explicit batch mode, literal semicolon/backslash escapes, and a trailing backslash. This is offline grammar evidence, not end-to-end hyprctl or compositor execution. Batch selection itself is established by the source gate above.

**Severity and corrections.** Low / conditional integration hazard. Passing untrusted text as a batch program can cause unintended command selection; automatically escaping the entire batch argument would also disable the documented command separator. The dossier's ordinary-dispatch example does not prove two IPC commands run. The `starts_with` command-boundary issue is independently covered by F-IPC-08; it does not enable the batch parser.

**Minimal fix direction.** Preserve the documented raw batch API and document escaping for literal data; prefer a structured multi-command API when callers must pass arbitrary argument values. Fix specific wrappers that mix external text with batch syntax. Do not apply blanket batch escaping to ordinary single-request arguments, where backslashes and semicolons can be legitimate data.

[Full dossier-level verification report](_evidence/full-audit-02-ipc-hyprctl.md) (shared upstream context and per-finding evidence).

[Raw audit dossier](../../pending-evaluation/2026-10-full-audit/02-ipc-hyprctl.md). These remain agent evidence, not human-confirmed material.
