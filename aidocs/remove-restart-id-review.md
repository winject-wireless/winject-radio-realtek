# Review: remove `reset id=` (winject-radio-realtek)

Reviewed: the uncommitted working tree on `main` (based on `0559f5d`), 10 files,
+21 / −113. Plan: [remove-restart-id.md](./remove-restart-id.md).

## Verdict

The code change is correct and complete. Fix one doc regression and the
vendoring bookkeeping before committing.

| # | Severity | Item |
|---|---|---|
| 1 | Medium | `implementation.md` §5 now points to the wrong source paths |
| 2 | Medium | The vendored copy is no longer the pinned `VERSION`, and it is not byte-identical to esp32 |
| 3 | Low | `docs/plan.md` still describes `reset id=` |
| 4 | Low | The `reset_id` unlink in the `Settings` constructor runs forever and has no comment |
| 5 | Nit | §5 sentence repeats `VERSION` |

## Verification

- `cmake --build build`, then `ctest`: 56/57 pass.
- The one failure, `ConfigTest.Defaults` (`ConfigTest.cpp:39`, `usb_port`
  is `""` but `"3-1.1"` is expected), already exists. This change does not
  touch `Config*`, `ConfigTest.cpp`, or `configuration/`.
- The test binary is newer than every edited source file.
  `ResetWithAndWithoutMode`, `CmdPrefixCorrelation`, and `SettingsTest.*` pass.
- Searching for `reset_id|reset id|id=<u8>|EALREADY` outside `aidocs/` finds
  nothing left in code. The remaining `EALREADY` / `mplane_status::already`
  uses are for `test_*`, and the comment at `mplane_backend.h:19` already
  says so. The remaining `id=<u8>` strings belong to `test_ether_tx` /
  `test_wifi_tx`. Doc hits are covered in item 3.

## What matches the plan

- `cmd_reset` (`src/vendor/mplane/mplane_commands.cpp:241`): `"id"` is taken
  out of `k_keys`, so `parse_args` rejects `id=` with `EINVAL` before
  `restart()` is called. The test checks both the reply and that
  `device.restarts` stays at 3.
- `accept_reset_id` is removed from `device_backend`, `RealtekDeviceBackend`,
  `Settings`, and the test fake. Nothing is left unused (`reset_path`,
  `reset_id_`, `reset_id_matches` are all gone).
- The reply is always plain `OK`. With a prefix it is `OK:8` (test updated).
- Help text: `reset` usage is now `[mode=WINJECT|OTA]`.
- Docs: `mplane.md` (correlation, the `EALREADY` row, the command table, the
  Reset paragraph describing the `ts` confirm, the example session),
  `winject.md` (Reset, `state.dir`, the state files table, the source map),
  and `implementation.md` (diagram, config table, source list, command
  table, test table, P2 row) are all updated.

## Findings

### 1. `implementation.md` §5 source path is now wrong (Medium)

`docs/implementation.md:130` was changed from
`winject-radio-esp32/src/winject-esp32/` to
`winject-radio-esp32/src/winject-esp32/mplane/`. However, the file list below
it is still relative to the old base:

```
mplane/mplane_args.{h,cpp}   ...   config_types.{h,cpp}
```

With the new base, this resolves to `.../mplane/mplane/mplane_args.*` and
`.../mplane/config_types.*`. Neither exists: `config_types.*` is in
`src/winject-esp32/`. Revert the base path to `src/winject-esp32/`.

### 2. Vendoring bookkeeping (Medium)

- `src/vendor/mplane/VERSION` is still `3dbab50`. At that commit, the
  vendored code still has `reset id=`. Anyone re-vendoring from `VERSION`
  would bring the id back.
- The esp32 change is uncommitted in `../winject-radio-esp32` (8 files).
  Diffing against that working tree, all vendored files match except
  `mplane_commands.cpp:90`. Realtek has the `reset` entry on one 85-column
  line. esp32 keeps it wrapped over two lines, as before, under 80
  columns. `.clang-format-ignore` excludes `src/vendor/mplane/`, so this edit
  was made by hand, not by the formatter.

Fix: commit the esp32 change first, copy the esp32 files over the vendored
ones as they are (with the line wrap), and set `VERSION` to the new esp32
commit. Copied files are then identical to the source by construction, and
this is the flow that §5 describes.

### 3. `docs/plan.md` still describes `reset id=` (Low)

- `:35`: `state.dir` "holds save/load slots and the last reset id".
- `:56`: the `reset id=` row (store id, `EALREADY`).
- `:88`: P2 lists "`reset id=`".

If `plan.md` is kept as a historical record, leave it and add a one-line note
that points to `aidocs/remove-restart-id.md`. Otherwise update these three
lines the same way as `implementation.md`.

### 4. `reset_id` cleanup in the `Settings` constructor (Low)

`src/radio/Settings.cpp:46` runs
`unlink((state_dir_ + "/reset_id").c_str());` on every construction.

- It works. `unlink` on a missing file or directory does nothing, and the
  return value is ignored on purpose. `<unistd.h>` is already included.
- It is migration code with no end date, in a constructor that otherwise does
  nothing. `SettingsTest` also triggers it, which is harmless.
- A stale `reset_id` file has no effect: nothing reads it.

Either drop the line (the simplest option, which the plan allows), or keep it
with a one-line comment such as `// pre-ts reset ids; remove after all units
upgraded`, so a later reader knows why it is there and when to delete it.

### 5. §5 wording (Nit)

"at a pinned commit (see `src/vendor/mplane/VERSION`), and record the commit
in `src/vendor/mplane/VERSION`" names the file twice. Suggested: "at a pinned
commit, and record that commit in `src/vendor/mplane/VERSION`".

## Not in scope, noted

- `docs/mplane.md` documents `reset` as `[mode=WINJECT]`, while the help
  string says `[mode=WINJECT|OTA]`. This was already the case: `OTA` is
  rejected with `EINVAL` on Realtek and documented that way.
- The esp32 working tree changes were not reviewed here, apart from the
  vendored-file diff in item 2.
