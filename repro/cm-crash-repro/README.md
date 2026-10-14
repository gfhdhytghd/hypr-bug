# cm-crash-repro — client for issue #1

Reproduction client for the two Critical SIGSEGV bugs described in
[`../../issues/2026-10-color-management-crashes.md`](../../issues/2026-10-color-management-crashes.md).

Build: `bash build.sh` (requires `wayland-scanner` and libwayland-client dev
headers; protocol stubs are regenerated on the fly).

Trigger sequences (details + success criteria in the dossier):

```
./repro feedback               # crash #1: feedback not inerted on surface destroy
hyprctl output create headless VICTIM
./repro getinfo VICTIM         # crash #2A: get_information on failed description
./repro poison  VICTIM         # crash #2B: failed description into the render loop
```
