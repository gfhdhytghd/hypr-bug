# cm-feedback-inert — client for the feedback-inertness crash

Triggers the SIGSEGV where `wp_color_management_surface_feedback_v1` is never
inerted after its `wl_surface` dies (unguarded expired-WP deref in
`onPreferredChanged`, `ColorManagement.cpp:460`).

Build with `bash build.sh`, then run `./repro` and follow the prompts.
Full analysis, spec citations and the fix: [`REPORT.md`](REPORT.md).
