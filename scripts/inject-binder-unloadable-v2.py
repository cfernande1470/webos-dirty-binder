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
    raise SystemExit("UNLOADABLE_V2_FAIL: binder.c not found")

s = binder.read_text(errors="replace")

if "DIRTY_BINDER_UNLOADABLE_V2" in s:
    print("UNLOADABLE_V2_ALREADY_PATCHED", binder)
    raise SystemExit(0)

if "static HLIST_HEAD(binder_devices)" in s or "HLIST_HEAD(binder_devices)" in s:
    foreach = "hlist_for_each_entry(device, &binder_devices, hlist)"
elif "static LIST_HEAD(binder_devices)" in s or "LIST_HEAD(binder_devices)" in s:
    foreach = "list_for_each_entry(device, &binder_devices, list)"
else:
    raise SystemExit("UNLOADABLE_V2_FAIL: cannot detect binder_devices list type")

exit_func = f'''
/* DIRTY_BINDER_UNLOADABLE_V2
 * Test-only unload path. Only use after killing binder users.
 */
static void __exit binder_dirty_unloadable_exit(void)
{{
	struct binder_device *device;

	pr_info("binder: DIRTY_BINDER_UNLOADABLE_V2 exit begin\\n");

	{foreach} {{
		pr_info("binder: dirty misc_deregister %s\\n",
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

	pr_info("binder: DIRTY_BINDER_UNLOADABLE_V2 exit done\\n");
}}
'''

# Replace final initcall/init macro.
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
        print("UNLOADABLE_V2_PATCHED", binder)
        raise SystemExit(0)

Path("/tmp/binder-tail-unloadable-v2.txt").write_text(s[-6000:])
raise SystemExit("UNLOADABLE_V2_FAIL: binder_init initcall not found; dumped /tmp/binder-tail-unloadable-v2.txt")
