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
    raise SystemExit("UNLOADABLE_V3_FAIL: binder.c not found")

s = binder.read_text(errors="replace")

if "DIRTY_BINDER_UNLOADABLE_V3" in s:
    print("UNLOADABLE_V3_ALREADY_PATCHED", binder)
    raise SystemExit(0)

# Detect device registration model.
cleanup_lines = []

if re.search(r'static\s+struct\s+miscdevice\s+binder_miscdev\s*=', s) or "binder_miscdev" in s:
    cleanup_lines.append('\tpr_info("binder: dirty misc_deregister binder_miscdev\\n");')
    cleanup_lines.append('\tmisc_deregister(&binder_miscdev);')
    model = "old binder_miscdev"
elif "binder_devices" in s and "hlist_for_each_entry" in s:
    cleanup_lines.append('\tstruct binder_device *device;')
    cleanup_lines.append('')
    cleanup_lines.append('\thlist_for_each_entry(device, &binder_devices, hlist) {')
    cleanup_lines.append('\t\tpr_info("binder: dirty misc_deregister %s\\n",')
    cleanup_lines.append('\t\t\tdevice->miscdev.name ? device->miscdev.name : "binder");')
    cleanup_lines.append('\t\tmisc_deregister(&device->miscdev);')
    cleanup_lines.append('\t}')
    model = "modern hlist binder_devices"
elif "binder_devices" in s and "list_for_each_entry" in s:
    cleanup_lines.append('\tstruct binder_device *device;')
    cleanup_lines.append('')
    cleanup_lines.append('\tlist_for_each_entry(device, &binder_devices, list) {')
    cleanup_lines.append('\t\tpr_info("binder: dirty misc_deregister %s\\n",')
    cleanup_lines.append('\t\t\tdevice->miscdev.name ? device->miscdev.name : "binder");')
    cleanup_lines.append('\t\tmisc_deregister(&device->miscdev);')
    cleanup_lines.append('\t}')
    model = "modern list binder_devices"
else:
    Path("/tmp/binder-tail-unloadable-v3.txt").write_text(s[-8000:])
    raise SystemExit("UNLOADABLE_V3_FAIL: no binder_miscdev or binder_devices model detected; dumped /tmp/binder-tail-unloadable-v3.txt")

# Optional cleanup only if symbols exist in this binder.c.
if "binder_debugfs_dir_entry_root" in s:
    cleanup_lines += [
        '',
        '#ifdef CONFIG_DEBUG_FS',
        '\tif (binder_debugfs_dir_entry_root)',
        '\t\tdebugfs_remove_recursive(binder_debugfs_dir_entry_root);',
        '#endif',
    ]

if "binder_deferred_workqueue" in s:
    cleanup_lines += [
        '',
        '\tif (binder_deferred_workqueue) {',
        '\t\tdestroy_workqueue(binder_deferred_workqueue);',
        '\t\tbinder_deferred_workqueue = NULL;',
        '\t}',
    ]

body = "\n".join(cleanup_lines)

exit_func = f'''
/* DIRTY_BINDER_UNLOADABLE_V3
 * Test-only unload path. Kill Binder users before rmmod.
 * Detected model: {model}
 */
static void __exit binder_dirty_unloadable_exit(void)
{{
\tpr_info("binder: DIRTY_BINDER_UNLOADABLE_V3 exit begin\\n");
{body}
\tpr_info("binder: DIRTY_BINDER_UNLOADABLE_V3 exit done\\n");
}}
'''

patterns = [
    r'device_initcall\s*\(\s*binder_init\s*\)\s*;',
    r'late_initcall\s*\(\s*binder_init\s*\)\s*;',
    r'module_init\s*\(\s*binder_init\s*\)\s*;',
]

for pat in patterns:
    m = re.search(pat, s)
    if m:
        s = s[:m.start()] + exit_func + "\nmodule_init(binder_init);\nmodule_exit(binder_dirty_unloadable_exit);\n" + s[m.end():]
        binder.write_text(s)
        print("UNLOADABLE_V3_PATCHED", binder, "model=", model)
        raise SystemExit(0)

Path("/tmp/binder-tail-unloadable-v3.txt").write_text(s[-8000:])
raise SystemExit("UNLOADABLE_V3_FAIL: binder_init initcall not found; dumped /tmp/binder-tail-unloadable-v3.txt")
