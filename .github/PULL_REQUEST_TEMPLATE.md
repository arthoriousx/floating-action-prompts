## Summary

<!-- Briefly describe what this PR changes and why. -->

## Related Issue

<!-- Closes #... (if applicable) -->

## Changes Made

-

## Verification Checklist

- [ ] Built in `Release` configuration (`cmake --build ... --config Release`)
- [ ] All 8 unit tests pass (`ctest -C Release --output-on-failure`)
- [ ] `MCM/Config/FloatingActionPrompts/config.json` parses cleanly as valid JSON
- [ ] Default INI files (`F4SE/Plugins/FloatingActionPrompts.ini` and `MCM/Config/FloatingActionPrompts/settings.ini`) stay synchronized with `src/Config.h`
- [ ] No compiled binaries (`*.dll`, `build/`), third-party clones (`external/`), or machine-specific local paths are included
