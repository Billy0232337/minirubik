#!/usr/bin/env python3
import argparse, ast, re
from pathlib import Path

def split_values(s):
    vals=[]; cur=''; in_q=False; esc=False
    for ch in s:
        if in_q:
            cur += ch
            if esc: esc=False
            elif ch=='\\': esc=True
            elif ch=='"': in_q=False
        else:
            if ch=='"': in_q=True; cur+=ch
            elif ch==',':
                if cur.strip(): vals.append(cur.strip())
                cur=''
            else: cur+=ch
    if cur.strip(): vals.append(cur.strip())
    return vals

def string_len(tok):
    # assembler strings are close enough to Python quoted string escapes here.
    try: return len(ast.literal_eval(tok).encode('latin1'))
    except Exception as e: raise ValueError(f'cannot parse string {tok!r}: {e}')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('asm')
    args=ap.parse_args()
    lines=Path(args.asm).read_text().splitlines()
    section=None; off=0; totals={'.data':0,'.bss':0,'.rodata':0}
    for raw in lines:
        line=raw.split('#',1)[0].strip()
        if not line: continue
        if line in ('.data','.bss','.rodata','.text'):
            if section in totals: totals[section]=off
            section=line
            off=0 if section in totals else off
            continue
        if section not in totals: continue
        # peel labels
        while ':' in line:
            before, after=line.split(':',1)
            if re.fullmatch(r'[A-Za-z_.$][\w.$]*', before.strip()):
                line=after.strip()
                if not line: break
            else: break
        if not line: continue
        if line.startswith('.align'):
            n=int(line.split()[1],0)
            a=1<<n
            off=(off+a-1)&-a
        elif line.startswith('.byte'):
            off += len(split_values(line[5:].strip()))
        elif line.startswith('.half'):
            off += 2*len(split_values(line[5:].strip()))
        elif line.startswith('.word'):
            off += 4*len(split_values(line[5:].strip()))
        elif line.startswith('.asciz'):
            vals=split_values(line[6:].strip())
            off += sum(string_len(v)+1 for v in vals)
        elif line.startswith('.ascii'):
            vals=split_values(line[6:].strip())
            off += sum(string_len(v) for v in vals)
        else:
            raise SystemExit(f'unsupported data directive: {line}')
    if section in totals: totals[section]=off
    total=sum(totals.values())
    limit=128*1024
    print(f".data   = {totals['.data']} bytes")
    print(f".bss    = {totals['.bss']} bytes")
    print(f".rodata = {totals['.rodata']} bytes")
    print(f"total   = {total} bytes")
    print(f"limit   = {limit} bytes")
    print(f"headroom= {limit-total} bytes")
    print('RESULT  = ' + ('PASS' if total <= limit else 'FAIL'))
    raise SystemExit(0 if total <= limit else 1)
if __name__=='__main__': main()
