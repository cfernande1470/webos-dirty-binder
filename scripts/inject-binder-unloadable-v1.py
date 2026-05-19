#!/usr/bin/env python3
from pathlib import Path
import sys
import re

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
    raise SystemExit("UNLOADABLE_V1_FAIL: binder.c not found")

s = binder.read_text(errors="replace")

if "DIRTY_BINDER_UNLOADABLE_V1" in s:
    print("UNLOADABLE_V1_ALREADY_PATCHED", binder)
    raise SystemExit(0)

if "binder_dirty_unloadable_exit" in s:
    print("UNLOADABLE_V1_ALREADY_HAS_EXIT", binder)
    raise SystemExit(0)

if "struct binder_device" not in s or "binder_devices" not in s or "miscdev" not in s:
    raise SystemExit("UNLOADABLE_V1_FAIL: binder device/miscdev structures not found")

if "HLIST_HEAD(binder_devices)" in s or "hlist_for_each_entry" in s:
    foreach = "hlist_for_each_entry(device, &binder_devices, hlist)"
elif "LIST_HEAD(binder_devices)" in s or "list_for_each_entry" in s:
    foreach = "list_for_each_entry(device, &binder_devices, list)"
else:
    raise SystemExit("UNLOADABLE_V1_FAIL: cannot determine binder_devices list type")

exit_func = f'''
/* DIRTY_BINDER_UNLOADABLE_V1
 * Test-only module exit path.
 * Use only after killing servicemanager/test clients.
 */
static void __exit binder_dirty_unloadable_exit(void)
{{
	struct binder_device *device;

	pr_info("binder: dirty unloadable exit begin\\n");

	{foreach} {{
		pr_info("binder: misc_deregister %s\\n",
			device->miscdev.name ? device->miscdev.name : "binder");
		misc_deregister(&device->miscdev);
	}}

#ifdef CONFIG_DEBUG_FS
	if (binder_debugfs_dir_entry_root)
		debugfs_remove_recursive(binder_debugfs_dir_entry_root);
#endif

	if (binder_deferred_workqueue) {{
		destroy_workqueue(binder_deferred_workqueue);
		binder_deferred_workqueue = NULL;
	}}

	pr_info("binder: dirty unloadable exit done\\n");
}}
'''

# Replace initcall with module_init + module_exit.
patterns = [
    r'device_initcall\s*\(\s*binder_init\s*\)\s*;',
    r'late_initcall\s*\(\s*binder_init\s*\)\s*;',
    r'module_init\s*\(\s*binder_init\s*\)\s*;',
]

for pat in patterns:
    m = re.search(pat, s)
    if m:
        replacement = exit_func + "\nmodule_init(binder_init);\nmodule_exit(binder_dirty_unloadable_exit);"
        s = s[:m.start()] + replacement + s[m.end():]
        binder.write_text(s)
        print("UNLOADABLE_V1_PATCHED", binder)
        raise SystemExit(0)

raise SystemExit("UNLOADABLE_V1_FAIL: binder_init initcall not found")
