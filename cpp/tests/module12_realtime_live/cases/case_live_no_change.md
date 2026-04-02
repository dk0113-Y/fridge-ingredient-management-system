# TC01_live_no_change

Scene:
- Keep the ROI stable.
- Allow only slight hand jitter or lighting noise.

Goal:
- The harness should ideally output `no_change`.
- If a motion window is opened, stage 1 and final JSON should not falsely claim a clear `put_in` or `take_out`.

Suggested command:
```bash
cpp/tests/module12_realtime_live/scripts/run_live_module12_test.sh --case-id TC01_live_no_change --module2-mode mock
```
