#!/bin/sh
set -eu

REPO_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_DIR/build/linux-4.4.84}"
CONFIG="${CONFIG:-$REPO_DIR/configs/lg-c1-o20-4.4.84-229.1.kavir.2.config}"
KERNEL_URL="${KERNEL_URL:-https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git}"
KERNEL_TAG="${KERNEL_TAG:-v4.4.84}"
KERNEL_RELEASE="${KERNEL_RELEASE:-4.4.84-229.1.kavir.2}"
ARTIFACT="$REPO_DIR/artifacts/binder-dirty-lgc1-o20-${KERNEL_RELEASE}.ko"

if [ ! -f "$CONFIG" ]; then
  echo "ERROR: kernel config not found: $CONFIG"
  exit 1
fi

mkdir -p "$(dirname "$BUILD_DIR")"

if [ ! -d "$BUILD_DIR/.git" ]; then
  echo "=== cloning Linux $KERNEL_TAG ==="
  git clone --depth 1 --branch "$KERNEL_TAG" "$KERNEL_URL" "$BUILD_DIR"
fi

cd "$BUILD_DIR"

echo "=== resetting kernel tree ==="
git reset --hard
git clean -fdx

echo "=== applying LG config ==="
cp "$CONFIG" .config

echo "=== applying webos-dirty-binder patch ==="
git apply "$REPO_DIR/patches/0001-lg-webos-dirty-binder-module.patch"

echo "=== copying dirty Binder header ==="
cp "$REPO_DIR/src/binder_dirty_exports.h" drivers/android/binder_dirty_exports.h

echo "=== injecting alloc shim for Binder mmap ==="
python3 - <<'PY'
from pathlib import Path
import re

p = Path("drivers/android/binder.c")
txt = p.read_text()

helper = '''
static struct page *binder_dirty_alloc_page_for_mmap(void)
{
	unsigned long addr;
	struct page *page;

	pr_err("binder_alloc_shim: before __get_free_page GFP_KERNEL=0x%x\\n", GFP_KERNEL);
	addr = __get_free_page(GFP_KERNEL);
	pr_err("binder_alloc_shim: after __get_free_page addr=0x%lx\\n", addr);
	if (!addr)
		return NULL;

	memset((void *)addr, 0, PAGE_SIZE);
	page = virt_to_page((void *)addr);
	pr_err("binder_alloc_shim: virt_to_page page=%p\\n", page);
	return page;
}

'''

if "binder_dirty_alloc_page_for_mmap" not in txt:
    marker = "static int binder_update_page_range"
    idx = txt.find(marker)
    if idx < 0:
        raise SystemExit("cannot find binder_update_page_range")
    txt = txt[:idx] + helper + txt[idx:]

txt, n = re.subn(r"alloc_page\s*\([^;]*\)", "binder_dirty_alloc_page_for_mmap()", txt)
print("alloc_page replacements=%d" % n)
if n == 0:
    print("WARNING: no alloc_page call replaced")

m = re.search(r"static int binder_mmap\(struct file \*filp, struct vm_area_struct \*vma\)\n\{", txt)
if m and "binder_alloc_shim: binder_mmap enter" not in txt:
    pos = m.end()
    insert = "\n\tpr_err(\"binder_alloc_shim: binder_mmap enter filp=%p vma=%p start=0x%lx end=0x%lx flags=0x%lx\\n\",\n\t       filp, vma, vma->vm_start, vma->vm_end, vma->vm_flags);\n"
    txt = txt[:pos] + insert + txt[pos:]

m = re.search(r"static int binder_update_page_range\((.*?)\)\n\{", txt, flags=re.S)
if m and "binder_alloc_shim: update_page_range enter" not in txt:
    pos = m.end()
    insert = "\n\tpr_err(\"binder_alloc_shim: update_page_range enter allocate=%d start=%p end=%p vma=%p\\n\",\n\t       allocate, start, end, vma);\n"
    txt = txt[:pos] + insert + txt[pos:]

if "binder_alloc_shim: before vm_insert_page" not in txt:
    txt = txt.replace(
        "ret = vm_insert_page(vma, user_page_addr, page[0]);",
        'pr_err("binder_alloc_shim: before vm_insert_page user_page_addr=0x%lx page=%p\\n", user_page_addr, page[0]);\n\t\t\tret = vm_insert_page(vma, user_page_addr, page[0]);\n\t\t\tpr_err("binder_alloc_shim: after vm_insert_page ret=%d\\n", ret);'
    )

p.write_text(txt)
PY

echo "=== configuring Binder module ==="
printf '' > .scmversion

./scripts/config --set-str CONFIG_LOCALVERSION "-229.1.kavir.2"
./scripts/config --disable CONFIG_LOCALVERSION_AUTO
./scripts/config --enable CONFIG_ANDROID
./scripts/config --module CONFIG_ANDROID_BINDER_IPC
./scripts/config --set-str CONFIG_ANDROID_BINDER_DEVICES "binder,hwbinder,vndbinder"


# webos-dirty-binder: task_euid current_euid transaction fix
python3 - <<'PY_TASK_EUID_FIX'
from pathlib import Path

p = Path("drivers/android/binder.c")
s = p.read_text()

old = "t->sender_euid = task_euid(proc->tsk);"
new = """/*
         * webOS dirty binder:
         * Avoid task_euid(proc->tsk) on LG webOS kernel.
         *
         * On this LG webOS 4.4.84 target, task_euid(proc->tsk)
         * NULL-dereferences during BC_TRANSACTION. For this PoC,
         * current_euid() keeps sender_euid meaningful for the task
         * issuing the ioctl and allows real Binder transactions.
         */
        t->sender_euid = current_euid();"""

if old in s:
    s = s.replace(old, new, 1)
    p.write_text(s)
    print("patched binder.c: task_euid(proc->tsk) -> current_euid()")
elif "t->sender_euid = current_euid();" in s:
    print("binder.c already has current_euid() sender_euid fix")
else:
    print("ERROR: could not find sender_euid task_euid line")
    for i, line in enumerate(s.splitlines(), 1):
        if "sender_euid" in line or "task_euid" in line or "current_euid" in line:
            print(f"{i}: {line}")
    raise SystemExit(1)
PY_TASK_EUID_FIX


make ARCH=arm64 HOSTCFLAGS="-fcommon" olddefconfig

echo "=== preparing kernel build files ==="
make ARCH=arm64 HOSTCFLAGS="-fcommon" modules_prepare

echo "=== forcing exact kernel release ==="
mkdir -p include/generated include/config
echo "#define UTS_RELEASE \"$KERNEL_RELEASE\"" > include/generated/utsrelease.h
echo "$KERNEL_RELEASE" > include/config/kernel.release

echo "=== building Binder module ==="
make ARCH=arm64 HOSTCFLAGS="-fcommon" -j"$(nproc)" M=drivers/android modules

if [ ! -f drivers/android/binder.ko ]; then
  echo "ERROR: drivers/android/binder.ko was not generated"
  exit 1
fi

echo "=== module info ==="
modinfo drivers/android/binder.ko | grep -E 'filename|name|vermagic|depends'
modinfo -p drivers/android/binder.ko || true

echo "=== checking debug strings ==="
strings drivers/android/binder.ko | grep -E 'binder_alloc_shim|binder_dirty' | head -n 80 || true

echo "=== checking known unresolved non-exported Binder symbols ==="
BAD_SYMBOLS='zap_page_range|put_files_struct|get_vm_area|__fd_install|__close_fd|map_kernel_range_noflush|__lock_task_sighand|get_files_struct|__alloc_fd|can_nice|security_binder_'
if nm -u drivers/android/binder.ko | sort | grep -E "$BAD_SYMBOLS"; then
  echo "ERROR: unresolved known non-exported Binder symbols remain"
  exit 1
fi

echo "OK: no known non-exported Binder symbols remain"

mkdir -p "$(dirname "$ARTIFACT")"
cp drivers/android/binder.ko "$ARTIFACT"
ls -lh "$ARTIFACT"
echo "Build completed: $ARTIFACT"
