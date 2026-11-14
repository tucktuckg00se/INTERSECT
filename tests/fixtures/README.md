# Saved-state fixtures

`IntersectProcessorTests state` loads every `*.bin` here: it checks that the state is accepted, that its
slices and samples come back (the samples are missing, so they load as placeholders), and that a re-save
loads back to the same bytes. Each `.json` sidecar holds what the test expects; `extract_state.py` writes both.

| Fixture | Layout | Saved with |
|---|---|---|
| `state-v24-ext3.bin` | state v24, extension 3 | v0.12-era build, REAPER project |
| `state-v24-ext6.bin` | state v24, extension 6 (legacy APVTS sample params) | v0.16.0, preset |
| `state-v24-ext7.bin` | state v24, extension 7 | v0.17-era build, REAPER project |
| `state-v24-ext8.bin` | state v24, extension 8 | v0.18-era build, REAPER project |

Not covered yet: extension 5 (v0.13.x) and states before v24 (v0.9 to v0.11).

Adding one:

```bash
python3 tests/fixtures/extract_state.py <file.intersectpreset | project.rpp> tests/fixtures/state-v<N>-ext<M>.bin
```

- Use a file saved by a build whose layout was released. A dev build from before the layout was
  committed may have written a different one.
- The script replaces sample paths and names with `/fixtures/sample-N.*`. Check the `.bin` has no
  other personal text (`strings <file>`) before committing it.
- Every state or extension version bump adds a fixture saved by the previous release.
