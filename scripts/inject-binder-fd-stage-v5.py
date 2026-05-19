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
    raise SystemExit("FD_STAGE_V11_FAIL: binder.c not found")

s = binder.read_text(errors="replace")

if "DIRTY_BINDER_FD_STAGE_V11" in s:
    print("FD_STAGE_V11_ALREADY_PATCHED", binder)
    raise SystemExit(0)

if "binder_dirty_fd_stage" not in s:
    param = """
/*
 * DIRTY_BINDER_FD_STAGE_V11
 *   0: reject before fget()
 *   1: fget() + fput(), reject
 *   2: reject before fd allocation
 *   3: diag only, no task_struct derefs
 *   4: __alloc_fd + __close_fd on proc->files
 *   5: __alloc_fd + __close_fd on target_proc->files
 *   6: target alloc + __fd_install + __close_fd, then reject
 * >=7: unsafe/reserved
 */
static int binder_dirty_fd_stage = 7;
module_param_named(fd_debug_stage, binder_dirty_fd_stage, int, 0644);
MODULE_PARM_DESC(fd_debug_stage, "Dirty Binder FD debug stage");
"""
    anchors = [
        "static DEFINE_MUTEX(binder_main_lock);",
        "static HLIST_HEAD(binder_devices);",
        "static int binder_debug_mask",
        "static const struct file_operations binder_fops",
    ]
    pos = -1
    for a in anchors:
        pos = s.find(a)
        if pos >= 0:
            break
    if pos < 0:
        raise SystemExit("FD_STAGE_V11_FAIL: no global anchor")
    s = s[:pos] + param + "\n" + s[pos:]

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

fd_cases = [m.start() for m in re.finditer(r'case\s+BINDER_TYPE_FD\s*:', s)]
chosen = None
dump = []

for n, idx in enumerate(fd_cases):
    bi, bj = case_block_bounds(s, idx)
    if bj < 0:
        continue
    block = s[bi:bj]
    dump.append(f"\n===== FD CASE {n} offset={idx} =====\n{block}\n")
    has_fget = re.search(r'\b[A-Za-z_][A-Za-z0-9_]*\s*=\s*(?:dirty_)?fget\s*\(', block) is not None
    has_alloc = ("task_get_unused_fd_flags" in block) or ("dirty___alloc_fd" in block)
    has_error = "err_fd_not_allowed" in s[bi:min(len(s), bj + 3000)]
    if has_fget and has_alloc and has_error:
        chosen = (bi, bj, block)
        break

if chosen is None:
    Path("/tmp/binder-fd-blocks-v11.txt").write_text("".join(dump))
    raise SystemExit("FD_STAGE_V11_FAIL: no real FD block found")

case_i, case_j, block = chosen
orig = block

m = re.search(
    r'(?P<indent>[ \t]*)(?P<lhs>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<fn>(?:dirty_)?fget)\s*\(\s*(?P<arg>[^;]+?)\s*\)\s*;',
    block,
)
if not m:
    raise SystemExit("FD_STAGE_V11_FAIL: no fget")

indent = m.group("indent")
lhs = m.group("lhs")
stmt = m.group(0)

stage01 = (
    f'{indent}if (binder_dirty_fd_stage == 0) {{\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=0 reject_before_fget\\n");\n'
    f'{indent}\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\tgoto err_fd_not_allowed;\n'
    f'{indent}}}\n\n'
    f'{stmt}\n\n'
    f'{indent}if (binder_dirty_fd_stage == 1) {{\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=1 after_fget file=%p\\n", {lhs});\n'
    f'{indent}\tif ({lhs})\n'
    f'{indent}\t\tfput({lhs});\n'
    f'{indent}\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\tgoto err_fd_not_allowed;\n'
    f'{indent}}}'
)
block = block[:m.start()] + stage01 + block[m.end():]

alloc_re = re.compile(
    r'(?P<indent>[ \t]*)(?P<fdvar>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<fn>task_get_unused_fd_flags|dirty___alloc_fd)\s*\([^;]*\)\s*;'
)

am = alloc_re.search(block)
if not am:
    raise SystemExit("FD_STAGE_V11_FAIL: no allocation call")

sec_re = re.compile(
    r'(?P<indent>[ \t]*)(?P<ret>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<fn>(?:dirty_)?security_binder_transfer_file)\s*\([^;]*\)\s*;'
)
sm = sec_re.search(block)

if sm:
    indent = sm.group("indent")
    ret = sm.group("ret")
    stmt = sm.group(0)
    stage2 = (
        f'{stmt}\n\n'
        f'{indent}if (binder_dirty_fd_stage == 2) {{\n'
        f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=2 after_security ret=%d file=%p\\n", {ret}, {lhs});\n'
        f'{indent}\tif ({ret} >= 0)\n'
        f'{indent}\t\t{ret} = -EPERM;\n'
        f'{indent}}}'
    )
    block = block[:sm.start()] + stage2 + block[sm.end():]
else:
    am = alloc_re.search(block)
    indent = am.group("indent")
    stage2 = (
        f'{indent}if (binder_dirty_fd_stage == 2) {{\n'
        f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=2 reject_before_alloc_no_security_hook file=%p\\n", {lhs});\n'
        f'{indent}\tif ({lhs})\n'
        f'{indent}\t\tfput({lhs});\n'
        f'{indent}\treturn_error = BR_FAILED_REPLY;\n'
        f'{indent}\tgoto err_fd_not_allowed;\n'
        f'{indent}}}\n\n'
    )
    block = block[:am.start()] + stage2 + block[am.start():]

am = alloc_re.search(block)
if not am:
    raise SystemExit("FD_STAGE_V11_FAIL: allocation disappeared")

indent = am.group("indent")

stage3456 = (
    f'{indent}if (binder_dirty_fd_stage == 3) {{\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=3 diag_no_task_deref proc=%p proc_files=%p target_proc=%p target_files=%p file=%p sym___alloc_fd=%px sym___fd_install=%px sym___close_fd=%px\\n",\n'
    f'{indent}\t       proc, proc ? proc->files : NULL, target_proc, target_proc ? target_proc->files : NULL, {lhs}, (void *)sym___alloc_fd, (void *)sym___fd_install, (void *)sym___close_fd);\n'
    f'{indent}\tif ({lhs}) fput({lhs});\n'
    f'{indent}\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\tgoto err_fd_not_allowed;\n'
    f'{indent}}}\n\n'

    f'{indent}if (binder_dirty_fd_stage == 4) {{\n'
    f'{indent}\tint fd = -999;\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=4 proc_files_alloc_probe proc=%p proc_files=%p file=%p\\n", proc, proc ? proc->files : NULL, {lhs});\n'
    f'{indent}\tif (proc && proc->files) {{\n'
    f'{indent}\t\tfd = dirty___alloc_fd(proc->files, 0, 1024, O_CLOEXEC);\n'
    f'{indent}\t\tpr_err("DIRTY_BINDER_FD_STAGE stage=4 proc_alloc_ret=%d\\n", fd);\n'
    f'{indent}\t\tif (fd >= 0) dirty___close_fd(proc->files, fd);\n'
    f'{indent}\t}}\n'
    f'{indent}\tif ({lhs}) fput({lhs});\n'
    f'{indent}\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\tgoto err_fd_not_allowed;\n'
    f'{indent}}}\n\n'

    f'{indent}if (binder_dirty_fd_stage == 5) {{\n'
    f'{indent}\tint fd = -999;\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=5 target_files_alloc_probe target_proc=%p target_files=%p file=%p\\n", target_proc, target_proc ? target_proc->files : NULL, {lhs});\n'
    f'{indent}\tif (target_proc && target_proc->files) {{\n'
    f'{indent}\t\tfd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC);\n'
    f'{indent}\t\tpr_err("DIRTY_BINDER_FD_STAGE stage=5 target_alloc_ret=%d\\n", fd);\n'
    f'{indent}\t\tif (fd >= 0) dirty___close_fd(target_proc->files, fd);\n'
    f'{indent}\t}}\n'
    f'{indent}\tif ({lhs}) fput({lhs});\n'
    f'{indent}\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\tgoto err_fd_not_allowed;\n'
    f'{indent}}}\n\n'

    f'{indent}if (binder_dirty_fd_stage == 6) {{\n'
    f'{indent}\tint fd = -999;\n'
    f'{indent}\tpr_err("DIRTY_BINDER_FD_STAGE stage=6 target_fd_install_probe target_proc=%p target_files=%p file=%p\\n", target_proc, target_proc ? target_proc->files : NULL, {lhs});\n'
    f'{indent}\tif (target_proc && target_proc->files && {lhs}) {{\n'
    f'{indent}\t\tfd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC);\n'
    f'{indent}\t\tpr_err("DIRTY_BINDER_FD_STAGE stage=6 target_alloc_ret=%d\\n", fd);\n'
    f'{indent}\t\tif (fd >= 0) {{\n'
    f'{indent}\t\t\tpr_err("DIRTY_BINDER_FD_STAGE stage=6 before_fd_install fd=%d file=%p\\n", fd, {lhs});\n'
    f'{indent}\t\t\tdirty___fd_install(target_proc->files, fd, {lhs});\n'
    f'{indent}\t\t\t{lhs} = NULL;\n'
    f'{indent}\t\t\tpr_err("DIRTY_BINDER_FD_STAGE stage=6 after_fd_install fd=%d closing_now\\n", fd);\n'
    f'{indent}\t\t\tdirty___close_fd(target_proc->files, fd);\n'
    f'{indent}\t\t\tpr_err("DIRTY_BINDER_FD_STAGE stage=6 after_close fd=%d\\n", fd);\n'
    f'{indent}\t\t}}\n'
    f'{indent}\t}}\n'
    f'{indent}\tif ({lhs}) fput({lhs});\n'
    f'{indent}\treturn_error = BR_FAILED_REPLY;\n'
    f'{indent}\tgoto err_fd_not_allowed;\n'
    f'{indent}}}\n\n'
)

block = block[:am.start()] + stage3456 + block[am.start():]

if block == orig:
    raise SystemExit("FD_STAGE_V11_FAIL: unchanged")

s = s[:case_i] + block + s[case_j:]
binder.write_text(s)
print("FD_STAGE_V11_INJECT_OK", binder)
