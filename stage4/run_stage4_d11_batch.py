#!/usr/bin/env python3
import argparse
import csv
import re
import subprocess
import sys
import time
from pathlib import Path


def pick(row, *names, default=None):
    for name in names:
        if name in row and row[name] not in (None, ""):
            return row[name]
    return default


def parse_int(row, *names, default=None):
    v = pick(row, *names, default=default)
    if v is None:
        return None
    return int(str(v).strip())


def parse_ripes_output(text: str):
    """Very tolerant parser for Ripes output, including ANSI/control prefixes."""
    # Ripes/Qt may emit terminal control sequences. They are invisible when
    # printed to a terminal but can break anchored string/regex matching.
    ansi = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
    clean = ansi.sub("", text).replace("\x00", "").replace("\r", "")

    def grab_key(key):
        # Search anywhere in the cleaned stream rather than requiring the key
        # to start at column zero.
        m = re.search(rf"{re.escape(key)}\s*=\s*(-?\d+)", clean, re.IGNORECASE)
        return int(m.group(1)) if m else None

    solution_length = grab_key("solution_length")
    root_heuristic = grab_key("root_heuristic")
    search_nodes = grab_key("search_nodes")

    iret = None
    lines = [ansi.sub("", line).replace("\x00", "").strip()
             for line in clean.split("\n")]
    for i, line in enumerate(lines):
        if "instructions retired" in line.lower():
            for candidate in lines[i + 1:]:
                candidate = candidate.strip()
                if not candidate:
                    continue
                m = re.search(r"-?\d+", candidate)
                if m:
                    iret = int(m.group(0))
                break
            if iret is not None:
                break

    return solution_length, root_heuristic, search_nodes, iret

def patch_root(template_text: str, p: int, o: int):
    # Patch only the first root-coordinate initializations.
    text, n1 = re.subn(r"(?m)^(\s*li\s+s10\s*,\s*)\d+(\s*(?:#.*)?)$",
                       rf"\g<1>{p}\g<2>", template_text, count=1)
    text, n2 = re.subn(r"(?m)^(\s*li\s+s11\s*,\s*)\d+(\s*(?:#.*)?)$",
                       rf"\g<1>{o}\g<2>", text, count=1)
    if n1 != 1 or n2 != 1:
        raise RuntimeError("Could not locate root 'li s10, ...' / 'li s11, ...' in template")
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ripes", required=True)
    ap.add_argument("--template", default="stage4_ida_fixed_ripes.s")
    ap.add_argument("--csv", default="d11_results.csv")
    ap.add_argument("--out", default="stage4_d11_iret_v5.csv")
    ap.add_argument("--limit", type=int)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--threshold", type=int, default=50_000_000)
    ap.add_argument("--no-resume", action="store_true")
    args = ap.parse_args()

    ripes = str(Path(args.ripes).expanduser())
    template_path = Path(args.template).resolve()
    input_csv = Path(args.csv).resolve()
    output_csv = Path(args.out).resolve()
    temp_asm = template_path.parent / ".stage4_batch_tmp.s"

    if not template_path.exists():
        print(f"ERROR: template not found: {template_path}", file=sys.stderr)
        return 2
    if not input_csv.exists():
        print(f"ERROR: CSV not found: {input_csv}", file=sys.stderr)
        return 2

    template = template_path.read_text()

    refs = []
    with input_csv.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rank = parse_int(row, "rank", "full_rank")
            state = pick(row, "state", "cube", "input", default="")
            exp_h = parse_int(row, "root_h", "root_heuristic", "heuristic", "h")
            exp_nodes = parse_int(row, "search_nodes", "nodes", "node_count")
            exp_len = parse_int(row, "solution_length", "distance", "exact_distance", "d", default=11)
            if rank is None:
                continue
            refs.append({
                "rank": rank,
                "state": state,
                "exp_h": exp_h,
                "exp_nodes": exp_nodes,
                "exp_len": exp_len,
            })

    if args.limit is not None:
        refs = refs[:args.limit]

    completed = set()
    if output_csv.exists() and not args.no_resume:
        try:
            with output_csv.open(newline="") as f:
                for row in csv.DictReader(f):
                    if row.get("status") == "PASS":
                        completed.add(int(row["rank"]))
        except Exception:
            pass

    write_header = not output_csv.exists() or args.no_resume
    out_mode = "w" if args.no_resume else "a"
    out_f = output_csv.open(out_mode, newline="")
    fields = [
        "rank", "state", "p", "o",
        "expected_length", "solution_length",
        "expected_h", "root_heuristic",
        "expected_nodes", "search_nodes",
        "instructions_retired", "status"
    ]
    writer = csv.DictWriter(out_f, fieldnames=fields)
    if write_header:
        writer.writeheader()

    start = time.time()
    passed = failed = skipped = 0
    max_iret = -1
    max_rank = None

    try:
        for idx, ref in enumerate(refs, 1):
            rank = ref["rank"]
            if rank in completed:
                skipped += 1
                continue

            p, o = divmod(rank, 729)
            temp_asm.write_text(patch_root(template, p, o))

            cmd = [
                ripes,
                "--mode", "cli",
                "--src", str(temp_asm),
                "-t", "asm",
                "--proc", "RV32_ISS",
                "--runinfo",
                "--iret",
                "--exectime",
            ]

            try:
                cp = subprocess.run(cmd, text=True, capture_output=True, timeout=args.timeout)
                output = (cp.stdout or "") + ("\n" + cp.stderr if cp.stderr else "")
            except subprocess.TimeoutExpired as e:
                output = (e.stdout or "") + "\n" + (e.stderr or "")
                cp = None

            got_len, got_h, got_nodes, got_iret = parse_ripes_output(output)

            reasons = []
            if cp is None:
                reasons.append("timeout")
            elif cp.returncode != 0:
                reasons.append(f"ripes_rc={cp.returncode}")

            if got_len is None:
                reasons.append("len=None")
            elif ref["exp_len"] is not None and got_len != ref["exp_len"]:
                reasons.append(f"len={got_len};exp{ref['exp_len']}")

            if got_h is None:
                reasons.append("h=None")
            elif ref["exp_h"] is not None and got_h != ref["exp_h"]:
                reasons.append(f"h={got_h};exp{ref['exp_h']}")

            if got_nodes is None:
                reasons.append("nodes=None")
            elif ref["exp_nodes"] is not None and got_nodes != ref["exp_nodes"]:
                reasons.append(f"nodes={got_nodes};exp{ref['exp_nodes']}")

            if got_iret is None:
                reasons.append("iret=None")
            elif got_iret > args.threshold:
                reasons.append(f"iret={got_iret}>gate{args.threshold}")

            status = "PASS" if not reasons else "FAIL:" + ";".join(reasons)

            writer.writerow({
                "rank": rank,
                "state": ref["state"],
                "p": p,
                "o": o,
                "expected_length": ref["exp_len"],
                "solution_length": got_len,
                "expected_h": ref["exp_h"],
                "root_heuristic": got_h,
                "expected_nodes": ref["exp_nodes"],
                "search_nodes": got_nodes,
                "instructions_retired": got_iret,
                "status": status,
            })
            out_f.flush()

            if status == "PASS":
                passed += 1
                if got_iret is not None and got_iret > max_iret:
                    max_iret = got_iret
                    max_rank = rank
            else:
                failed += 1
                print(f"FAIL rank={rank} state={ref['state']} p={p} o={o}: {status}", flush=True)
                print("----- Ripes raw output -----", flush=True)
                print(output, flush=True)
                print("----------------------------", flush=True)
                clean_debug = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", output).replace("\x00", "").replace("\r", "")
                print("----- normalized lines (repr) -----", flush=True)
                for dbg_line in clean_debug.split("\n")[:30]:
                    print(repr(dbg_line), flush=True)
                print("-----------------------------------", flush=True)

            if idx % 25 == 0 or idx == len(refs):
                elapsed = time.time() - start
                done = passed + failed + skipped
                rate = done / elapsed if elapsed > 0 else 0.0
                eta = (len(refs) - done) / rate if rate > 0 else 0.0
                print(
                    f"progress {done}/{len(refs)} pass={passed} fail={failed} skipped={skipped} "
                    f"max_iret={max_iret} elapsed={elapsed:.1f}s eta={eta:.1f}s",
                    flush=True,
                )
    finally:
        out_f.close()
        try:
            temp_asm.unlink()
        except OSError:
            pass

    print("\n===== Stage 4 distance-11 batch summary =====")
    print(f"total reference states : {len(refs)}")
    print(f"new PASS               : {passed}")
    print(f"FAIL                   : {failed}")
    print(f"resumed/skipped        : {skipped}")
    print(f"instruction gate       : {args.threshold}")
    print(f"max instructions       : {max_iret}")
    print(f"max-instruction rank   : {max_rank}")
    print(f"results CSV            : {output_csv}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
