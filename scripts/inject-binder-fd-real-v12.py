#!/usr/bin/env python3
from pathlib import Path
import re
import sys

roots = []
if len(sys.argv) > 1:
    roots.append(Path(sys.argv[1]).resolve())
roots.append(Path.cwd().resolve())

candidates = []
for r in roots:
    candidates += [
        r / "drivers/android/binder.c",
        r / "drivers/staging/android/binder.c",
        r / "build/linux-4.4.84/drivers/android/binder.c",
        r / "build/linux-4.4.84/drivers/staging/android/binder.c",
    ]

binder = next((c for c in candidates if c.exists()), None)
if binder is None:
    raise SystemExit("FD_REAL_V12_FAIL: binder.c not found")

s = binder.read_text(errors="replace")

if "DIRTY_BINDER_FD_REAL_STAGE_V12" in s:
    print("FD_REAL_V12_ALREADY_PATCHED", binder)
    raise SystemExit(0)

# Find real BINDER_TYPE_FD block already patched by v11.
fd_cases = [m.start() for m in re.finditer(r'case\s+BINDER_TYPE_FD\s*:', s)]
if not fd_cases:
    raise SystemExit("FD_REAL_V12_FAIL: no BINDER_TYPE_FD")

def case_block_bounds(src, idx):
    line_start = src.rfind("\n", 0, idx) + 1
    indent = src[line_start:idx]
    starts = []
    for token in ["case BINDER_TYPE_", "default:"]:
        j = src.find("\n" + indent + token, idx + 1)
        if j >= 0:
            starts.append(j)
    if starts:
        return idx, min(starts)
    j = src.find("\n" + indent + "break;", idx + 1)
    if j >= 0:
        return idx, j + len("\n" + indent + "break;")
    return idx, -1

chosen = None
for idx in fd_cases:
    bi, bj = case_block_bounds(s, idx)
    if bj < 0:
        continue
    block = s[bi:bj]
    if (
        "DIRTY_BINDER_FD_STAGE stage=6 target_fd_install_probe" in block
        and "fget" in block
        and "err_fd_not_allowed" in s[bi:min(len(s), bj + 3000)]
    ):
        chosen = (bi, bj, block)
        break

if chosen is None:
    Path("/tmp/binder-fd-real-v12-blocks.txt").write_text(
        "\n\n".join(s[case_block_bounds(s, idx)[0]:case_block_bounds(s, idx)[1]]
                    for idx in fd_cases if case_block_bounds(s, idx)[1] > 0)
    )
    raise SystemExit("FD_REAL_V12_FAIL: real patched FD block not found; dumped /tmp/binder-fd-real-v12-blocks.txt")

case_i, case_j, block = chosen

# Capture file variable from fget.
m_fget = re.search(
    r'(?P<indent>[ \t]*)(?P<lhs>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:dirty_)?fget\s*\([^;]+?\)\s*;',
    block,
)
if not m_fget:
    raise SystemExit("FD_REAL_V12_FAIL: fget variable not found")

lhs = m_fget.group("lhs")

# Find original allocation statement AFTER stage 6 block.
pos6 = block.find('DIRTY_BINDER_FD_STAGE stage=6 after_close')
if pos6 < 0:
    raise SystemExit("FD_REAL_V12_FAIL: stage6 after_close marker missing")

alloc_re = re.compile(
    r'(?P<indent>[ \t]*)(?P<fdvar>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<fn>task_get_unused_fd_flags|dirty___alloc_fd)\s*\([^;]*\)\s*;'
)

m_alloc = alloc_re.search(block, pos6)
if not m_alloc:
    Path("/tmp/binder-fd-real-v12-chosen.txt").write_text(block)
    raise SystemExit("FD_REAL_V12_FAIL: original allocation after stage6 not found; dumped /tmp/binder-fd-real-v12-chosen.txt")

indent = m_alloc.group("indent")
fdvar = m_alloc.group("fdvar")

stage7 = (
    f'{indent}/* DIRTY_BINDER_FD_REAL_STAGE_V12 */\n'
    f'{indent}if (binder_dirty_fd_stage == 7) {{\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=7 real_fd_path target_proc=%p target_files=%p file=%p\\n",\n'
    f'{indent}\t       target_proc, target_proc ? target_proc->files : NULL, {lhs});\n'
    f'{indent}\tif (!target_proc || !target_proc->files || !{lhs}) {{\n'
    f'{indent}\t\tpr_err("DIRTY_BINDER_FD_STAGE stage=7 missing target/files/file\\n");\n'
    f'{indent}\t\tif ({lhs})\n'
    f'{indent}\t\t\tfput({lhs});\n'
    f'{indent}\t\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\t\tgoto err_fd_not_allowed;\n'
    f'{indent}\t}}\n'
    f'{indent}\t{fdvar} = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC);\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=7 alloc_ret=%d\\n", {fdvar});\n'
    f'{indent}\tif ({fdvar} < 0) {{\n'
    f'{indent}\t\tfput({lhs});\n'
    f'{indent}\t\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\t\tgoto err_fd_not_allowed;\n'
    f'{indent}\t}}\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=7 before_fd_install fd=%d file=%p\\n", {fdvar}, {lhs});\n'
    f'{indent}\tdirty___fd_install(target_proc->files, {fdvar}, {lhs});\n'
    f'{indent}\t{lhs} = NULL;\n'
    f'{indent}\tfp->handle = {fdvar};\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=7 installed fd=%d fp_handle=%lu\\n", {fdvar}, (unsigned long)fp->handle);\n'
    f'{indent}\tbreak;\n'
    f'{indent}}}\n\n'
)

block = block[:m_alloc.start()] + stage7 + block[m_alloc.start():]
s = s[:case_i] + block + s[case_j:]

binder.write_text(s)
print("FD_REAL_V12_PATCHED", binder)
