#!/usr/bin/env python3
"""
Generate a Ripes-assemblable Stage 4 source from stage4_solver_final.s.in.

The pinned Ripes assembler used for this assignment does not implement
`.if` / `.endif`, so the canonical source keeps the renderer blocks marked
with those directives and this tiny host-side build step resolves them
before Ripes sees the file.

Examples:
    python3 build_stage4.py --render 0
    python3 build_stage4.py --render 1

Outputs:
    stage4_solver_final_cli.s
    stage4_solver_final_gui.s
"""
from pathlib import Path
import argparse
import re

def preprocess(text: str, render: int) -> str:
    out = []
    active_stack = [True]

    for raw in text.splitlines():
        stripped = raw.strip()

        if stripped == ".if RENDER":
            active_stack.append(active_stack[-1] and bool(render))
            continue

        if stripped == ".endif":
            if len(active_stack) == 1:
                raise SystemExit("error: unmatched .endif")
            active_stack.pop()
            continue

        if active_stack[-1]:
            # Keep the canonical switch visible in the generated file,
            # but set it to the actual build value.
            if re.match(r"^\s*\.equ\s+RENDER\s*,", raw):
                out.append(f".equ RENDER, {render}")
            else:
                out.append(raw)

    if len(active_stack) != 1:
        raise SystemExit("error: unterminated .if RENDER")

    return "\n".join(out) + "\n"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", type=int, choices=(0,1), required=True)
    ap.add_argument("--src", default="stage4_solver_final.s.in")
    ap.add_argument("--out")
    args = ap.parse_args()

    src = Path(args.src)
    if not src.exists():
        raise SystemExit(f"error: {src} not found")

    if args.out:
        out = Path(args.out)
    else:
        out = Path("stage4_solver_final_gui.s" if args.render
                   else "stage4_solver_final_cli.s")

    generated = preprocess(src.read_text(), args.render)
    out.write_text(generated)

    # Fast sanity checks.
    if ".if RENDER" in generated or ".endif" in generated:
        raise SystemExit("error: conditional directives leaked into output")

    if args.render == 0 and "LED_MATRIX_0_" in generated:
        raise SystemExit(
            "error: CLI build still contains LED_MATRIX_0_* references"
        )

    print(f"generated {out} (RENDER={args.render})")

if __name__ == "__main__":
    main()
