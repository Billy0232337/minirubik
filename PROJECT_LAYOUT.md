# Project layout

- `stage1_ripes_benchmark/` — Stage 1 Ripes memory/throughput microbenchmarks.
- `stage2/` — Algorithm-selection prototypes and exhaustive validators.
- `stage3/` — C optimization milestones (`v1`, `v2`, `v3`). These versioned source files are intentionally kept because the report discusses each optimization step.
  - `stage3/reference/` — clean freestanding GCC RV32I reference sources used for the Stage 4 comparison. Build products are ignored.
- `stage4/` — final hand-written RV32I implementation and validation.
  - `stage4_solver_final.s.in` — canonical final Stage 4 source.
  - `build_stage4.py` — generates CLI (`RENDER=0`) and GUI (`RENDER=1`) assembly. Generated `.s` files are ignored.
  - `stage4_heuristic_smoke.s`, `stage4_transition_smoke.s` — focused assembly smoke tests.
  - `stage4_ida_fixed_ripes.s`, `stage4_ida_worst_ripes.s` — retained Stage 4 regression fixtures used by the batch-validation workflow.
  - `d11_results.csv`, `stage4_d11_iret.csv` — retained measured validation results.
  - `final_led_animation.mp4` — final visual demonstration.

The old split `stage4_solver_preled.s` / `stage4_solver_led_final.s` files are superseded by the canonical source plus `build_stage4.py` and are removed in this cleaned layout.
