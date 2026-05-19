# webos-dirty-binder

> USB-only Android sidecar + unloadable dirty Binder module + real Android Binder FD passing on LG webOS
>
> Milestone date: 2026-05-19
>
> Tested target: LG webOS TV, `Linux 4.4.84-229.1.kavir.2`, `aarch64`
>
> Control host: NanoPi R3S
>
> Final validated Binder FD result:
>
> ```text
> sent_count=16
> received_count=16
> read_count=16
> round_count=16
> expected=16
> BINDER_FD_SMOKE_TV_OK
> BINDER_FD_STAGE7_REAL_FD_OK
> ```
>
> Final validated unload result:
>
> ```text
> binder 118784 0 - Live ...
> rmmod rc=0
> binder unloaded OK
> ```

---

## 1. Executive summary

`webos-dirty-binder` is an experimental bring-up project for running enough Android userspace on a rooted LG webOS TV to start Android framework components and use Android Binder semantics on top of the TV's existing Linux/webOS kernel.

The project is intentionally narrow and practical:

* keep Android state on USB storage;
* load a patched Binder module into the webOS kernel;
* expose Binder device nodes needed by Android userspace;
* mount a minimal Android rootfs from Waydroid/Lineage images;
* start the real Android `servicemanager`;
* validate Binder transactions, handle lifetime, and FD transfer.

The final blocker for this milestone was Binder file-descriptor passing. Binder basic transactions worked, `servicemanager` worked, service handles could be acquired with explicit `BC_ACQUIRE`, and Binder mmap had been repaired. The remaining failure was `BINDER_TYPE_FD`: attempting to use the old FD translation path could freeze the TV.

The final fix is:

```text
Do not use target_proc->tsk or current->files for FD allocation on this webOS kernel.

Use target_proc->files directly:

    file = fget(fp->handle)
    target_fd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC)
    dirty___fd_install(target_proc->files, target_fd, file)
    fp->handle = target_fd
```

The module is also now test-unloadable. The original module was marked `[permanent]` in `/proc/modules` because it had no `module_exit()`. That made every Binder experiment require a TV reboot. The final build adds a controlled test-only exit path and `module_exit()`, so `rmmod binder` works after Binder users are stopped.

---

## 2. Current project status

### 2.1 Working

The following pieces are validated:

```text
USB ext4 storage
Android system.img downloaded to USB
Android vendor.img downloaded to USB
Android rootfs assembled under /tmp/android-usb/android-rootfs
/system mounted from system.img
/vendor mounted from vendor.img
/apex mounted from system.img when present
/data on USB
/cache on USB
/proc mounted inside Android rootfs
/sys mounted inside Android rootfs
/dev bind-mounted into Android rootfs
dirty binder.ko loaded on the webOS kernel
/dev/binder created
Binder mmap works
Binder basic transaction path works
real Android 13 /system/bin/servicemanager starts and remains alive
mini servicemanager test harness works
Binder handle acquisition works
explicit BC_INCREFS / BC_ACQUIRE works
BINDER_TYPE_FD works through target_proc->files
Binder FD smoke test passes 16/16 rounds
binder.ko is unloadable for iterative development
```

Main USB mount:

```text
/tmp/android-usb
```

Main Android rootfs:

```text
/tmp/android-usb/android-rootfs
```

Main image paths:

```text
/tmp/android-usb/android-images/system.img
/tmp/android-usb/android-images/vendor.img
```

Main sidecar path:

```text
/tmp/android-usb/android-sidecar
```

Main logs:

```text
/tmp/android-usb/android-sidecar/logs/android-usb-install.log
/tmp/android-usb/android-sidecar/logs/servicemanager.log
```

### 2.2 Still pending

The following pieces are still outside this milestone:

```text
full /dev/hwbinder validation
full /dev/vndbinder validation
hwservicemanager bring-up
vndservicemanager bring-up
generated /linkerconfig/ld.config.txt
Android property service
real Android init or a controlled mini-init
zygote
system_server
SurfaceFlinger
Android UI inside a webOS app container
input/audio/network integration
SELinux-equivalent integration
long-running stress and leak tests
```

The Binder FD milestone is necessary for later Android framework components, but it does not by itself mean a full Android system is running.

---

## 3. Repository layout

Important directories:

```text
configs/
  android-usb.env
  lg-c1-o20-4.4.84-229.1.kavir.2.config

patches/
  0001-lg-webos-dirty-binder-module.patch

scripts/
  build-module.sh
  install-android-usb.sh
  tail-android-usb.sh
  diagnose-android-usb.sh
  build-sidecar.sh

src/
  binder_dirty_exports.h

tools/
  mini servicemanager, FD smoke, bridge, and diagnostic utilities

artifacts/
  optional archived modules; normal installers should prefer build/ output
```

The normal build output is:

```text
build/linux-4.4.84/drivers/android/binder.ko
```

The installers and test loaders must prefer this file over historical artifacts. Archived files under `artifacts/` can be useful for reference, but they are dangerous as an automatic default because they may not include the final FD fix or `module_exit()`.

---

## 4. Important final design decisions

### 4.1 USB-only Android state

Android state belongs on USB, not `/media/internal`.

webOS TVs may have a small internal storage partition. Android images, logs, `/data`, `/cache`, and rootfs mount points can fill it quickly. The stable project path is therefore:

```text
/tmp/android-usb
```

The real webOS automount path may be something like:

```text
/tmp/usb/sda/sda1
```

The scripts normalize that into `/tmp/android-usb`.

### 4.2 Real Android servicemanager by default

The milestone path starts the real Android binary:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

The old mini service manager remains useful for Binder smoke tests, especially FD-passing tests, but the normal Android USB installer should start the real Android `servicemanager` by default.

Normal default:

```text
START_SERVICEMANAGER=1
```

### 4.3 Binder module must be unloadable during development

Early builds loaded as:

```text
binder 118784 0 [permanent], Live ...
```

That is not a process-holder problem. It means the module has no unload/cleanup path. `rmmod binder` cannot remove a permanent module even with refcount `0`.

The final development module must show:

```text
binder 118784 0 - Live ...
```

and:

```text
rmmod binder
```

must succeed after Binder users are stopped.

The exit path is deliberately test-oriented:

```text
misc_deregister(&binder_miscdev)
destroy_workqueue(binder_deferred_workqueue) when present
debugfs_remove_recursive(...) when present
```

This is intended for the dirty Binder development module, not as a statement that upstream Android Binder can always be safely removed under load.

### 4.4 Do not use `task_struct` fields from this build tree

The LG webOS target kernel is close to Linux 4.4.84 but not layout-identical to the public build tree. During debugging, direct reads from `current` and `target_proc->tsk` produced suspicious values:

```text
pid=0
empty or corrupt comm
```

That was a strong warning that task layout assumptions were unsafe in this module.

The final FD path therefore avoids:

```text
target_proc->tsk->files
current->files
task_get_unused_fd_flags(target_proc->tsk, ...)
```

and uses the `files_struct` pointer already stored in Binder's own `binder_proc`:

```text
target_proc->files
```

---

## 5. Kernel symbol loading

The dirty Binder module needs access to non-exported kernel helpers. The project resolves those addresses from `/proc/kallsyms` on the TV and passes them as module parameters at `insmod`.

Required symbols:

```text
zap_page_range
put_files_struct
get_vm_area
__fd_install
__close_fd
map_kernel_range_noflush
__lock_task_sighand
get_files_struct
__alloc_fd
```

Typical load parameters:

```text
sym_zap_page_range=...
sym_put_files_struct=...
sym_get_vm_area=...
sym___fd_install=...
sym___close_fd=...
sym_map_kernel_range_noflush=...
sym___lock_task_sighand=...
sym_get_files_struct=...
sym___alloc_fd=...
```

The module exposes these read-only parameters under:

```text
/sys/module/binder/parameters/
```

A healthy loaded module shows non-zero values.

---

## 6. Binder mmap repair

The original Binder mmap path could fail because the webOS target kernel did not behave like the clean upstream 4.4 tree at the exact points Binder expected.

The module build injects a dirty mmap allocation shim:

```text
binder_dirty_alloc_page_for_mmap()
```

The shim logs:

```text
binder_alloc_shim: binder_mmap enter ...
binder_alloc_shim: update_page_range enter ...
binder_alloc_shim: before __get_free_page ...
binder_alloc_shim: after __get_free_page ...
binder_alloc_shim: virt_to_page ...
binder_alloc_shim: before vm_insert_page ...
binder_alloc_shim: after vm_insert_page ret=0
```

Successful Binder client/server startup depends on this mmap path.

---

## 7. Binder handle lifetime debugging

Before FD passing was fixed, handle lifetime had to be proven separately.

The important validated sequence was:

```text
client receives service handle
client sends explicit BC_INCREFS
client sends explicit BC_ACQUIRE
client frees getService reply buffer
client sends PING to acquired handle
server replies
```

The successful marker was:

```text
BINDER_HANDLE_ACQUIRE_OK
BINDER_HANDLE_ACQUIRE_SELFTEST_OK
```

This proved that the earlier FD failure was not caused by a stale handle after freeing the service-manager reply.

---

## 8. Binder FD debugging timeline

### 8.1 Initial symptom

Direct `BINDER_TYPE_FD` transactions failed or could freeze the TV.

The client could send an object like:

```text
BINDER_FD_OBJECT_SENT label=round-0 payload=payload-0-from-client fd=4
```

but the reply could become:

```text
BR_FAILED_REPLY
```

or the system could hang.

### 8.2 Stage 0: reject before `fget`

Stage 0 inserted:

```text
reject_before_fget
```

Result:

```text
DIRTY_BINDER_FD_STAGE stage=0 reject_before_fget
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

This proved the FD object reached the kernel path.

### 8.3 Stage 1: `fget` + `fput`

Stage 1 inserted:

```text
file = fget(fp->handle)
fput(file)
reject
```

Result:

```text
DIRTY_BINDER_FD_STAGE stage=1 after_fget file=...
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

This proved the source FD lookup was safe.

### 8.4 Stage 2: reject before allocation

Stage 2 rejected before FD allocation in the target process.

Result:

```text
DIRTY_BINDER_FD_STAGE stage=2 reject_before_alloc_no_security_hook file=...
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

This proved the crash was beyond `fget`.

### 8.5 Early Stage 3: freeze in allocation

The first stage 3 attempted the old allocation path and froze the TV. That isolated the problem to the target FD allocation path.

### 8.6 Pre-allocation diagnostics

A safer stage 3 printed target state before allocation. It showed:

```text
target_proc != NULL
target_proc->files != NULL
target_proc->tsk suspicious
task_pid_nr(target_proc->tsk) = 0
target comm empty or corrupt
```

That shifted the fix away from `target_proc->tsk`.

### 8.7 Source/target allocation probes

Probes showed:

```text
__alloc_fd(proc->files, ...) works
__close_fd(proc->files, fd) works
__alloc_fd(target_proc->files, ...) works
__close_fd(target_proc->files, fd) works
```

The important markers were:

```text
stage=4 proc_alloc_ret=5
stage=5 target_alloc_ret=4
```

### 8.8 Install + close probe

Stage 6 installed the file in the target and immediately closed it:

```text
dirty___alloc_fd(target_proc->files, ...)
dirty___fd_install(target_proc->files, fd, file)
dirty___close_fd(target_proc->files, fd)
```

Result:

```text
stage=6 target_alloc_ret=4
stage=6 before_fd_install fd=4
stage=6 after_fd_install fd=4 closing_now
stage=6 after_close fd=4
BINDER_FD_STAGE_SAFE_FAILURE_OK
```

That proved `__fd_install` was safe when used with `target_proc->files`.

### 8.9 Real FD path

Stage 7 became the real FD path:

```text
file = fget(fp->handle)
target_fd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC)
dirty___fd_install(target_proc->files, target_fd, file)
file = NULL
fp->handle = target_fd
```

Result:

```text
sent_count=16
received_count=16
read_count=16
round_count=16
expected=16
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

This is the milestone fix.

---

## 9. Final Binder FD implementation

The final implementation should not depend on the old `task_get_unused_fd_flags()` path. It should use the already-validated target Binder process file table.

Conceptually:

```c
case BINDER_TYPE_FD: {
    int target_fd;
    struct file *file;

    file = fget(fp->handle);
    if (!file) {
        return_error = BR_FAILED_REPLY;
        goto err_fget_failed;
    }

    if (!target_proc || !target_proc->files) {
        fput(file);
        return_error = BR_FAILED_REPLY;
        goto err_fd_not_allowed;
    }

    target_fd = dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC);
    if (target_fd < 0) {
        fput(file);
        return_error = BR_FAILED_REPLY;
        goto err_fd_not_allowed;
    }

    dirty___fd_install(target_proc->files, target_fd, file);
    file = NULL;
    fp->handle = target_fd;
    break;
}
```

The exact error labels and variable names depend on the local Binder 4.4 source, so the repo uses injectors that patch the generated kernel tree after applying the base dirty Binder patch.

---

## 10. Build workflow

### 10.1 Build Binder

```sh
cd /home/pi/disk/webos-dirty-binder
KCFLAGS="-Wno-error -Wno-error=unused-variable -Wno-error=unused-function" ./scripts/build-module.sh
```

Expected output:

```text
build/linux-4.4.84/drivers/android/binder.ko
```

Verify:

```sh
modinfo -p build/linux-4.4.84/drivers/android/binder.ko
readelf -sW build/linux-4.4.84/drivers/android/binder.ko | grep cleanup_module
```

Expected:

```text
fd_debug_stage:Dirty Binder FD debug stage (int)
cleanup_module
```

The `cleanup_module` symbol matters because without it the module will load as `[permanent]`.

### 10.2 Build sidecar tests

```sh
./scripts/build-sidecar.sh
```

Useful outputs include:

```text
build/mini_servicemgr_static
build/android_like_fd_passing_service_static
build/android_like_fd_passing_client_static
```

These are copied into:

```text
/tmp/android-usb/android-sidecar/bin/
```

under names expected by the FD smoke runner:

```text
mini_servicemgr
android_like_fd_passing_service
android_like_fd_passing_client
```

---

## 11. Loading Binder on the TV

### 11.1 Load after reboot

```sh
TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 scripts/reboot-tv-and-load-build-binder.sh
```

Expected:

```text
LOAD_BUILD_UNLOADABLE_BINDER_OK
```

Verify:

```sh
ssh root@192.168.2.121 'grep "^binder " /proc/modules; cat /sys/module/binder/parameters/fd_debug_stage'
```

Expected:

```text
binder 118784 0 - Live ...
7
```

Not expected:

```text
binder 118784 0 [permanent], Live ...
```

### 11.2 Reload without reboot

After the module is unloadable:

```sh
TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 scripts/reload-build-binder-tv.sh
```

This script refuses to reuse a stuck old module. If `rmmod binder` fails, it exits and tells you to reboot.

### 11.3 Unload

```sh
ssh root@192.168.2.121 '
killall servicemanager hwservicemanager vndservicemanager mini_servicemgr android_like_fd_passing_service android_like_fd_passing_client 2>/dev/null || true
sleep 1
rmmod binder
echo "rmmod rc=$?"
grep "^binder " /proc/modules || echo "binder unloaded OK"
'
```

Expected:

```text
rmmod rc=0
binder unloaded OK
```

---

## 12. FD smoke testing

### 12.1 One round

```sh
ROUNDS=1 NO_BUILD=1 TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 FD_STAGE_TIMEOUT=20 scripts/run-binder-fd-stage-tv.sh
```

Expected:

```text
BINDER_FD_STAGE7_REAL_FD_OK
```

### 12.2 Sixteen rounds

```sh
ROUNDS=16 NO_BUILD=1 TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 FD_STAGE_TIMEOUT=40 scripts/run-binder-fd-stage-tv.sh
```

Expected:

```text
sent_count=16
received_count=16
read_count=16
round_count=16
expected=16
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
```

### 12.3 What the service validates

The FD smoke service validates that it receives the expected number of Binder FD transactions, reads from the received descriptors, and observes the expected payload strings.

Passing counts mean the FD was not only installed in the target file table, but actually usable by the target process.

---

## 13. Android USB install

Normal install:

```sh
TV_IP=192.168.2.121 ./scripts/install-android-usb.sh
```

Tail logs:

```sh
TV_IP=192.168.2.121 ./scripts/tail-android-usb.sh
```

Diagnose:

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

The installer should:

```text
prefer build/linux-4.4.84/drivers/android/binder.ko
copy binder.ko to /tmp/android-usb/android-sidecar/modules/binder.ko
load binder.ko with symbol parameters
set fd_debug_stage=7 by default
create /dev/binder, /dev/hwbinder, /dev/vndbinder when present
mount Android rootfs
run toybox smoke
start real Android /system/bin/servicemanager when START_SERVICEMANAGER=1
```

Expected service-manager marker:

```text
ANDROID_REAL_SERVICEMANAGER_RUNNING pid=...
ANDROID_USB_INSTALL_DONE
```

---

## 14. Troubleshooting

### 14.1 `binder [permanent]`

Symptom:

```text
binder 118784 0 [permanent], Live ...
rmmod: Device or resource busy
```

Cause:

```text
The loaded module has no module_exit()/cleanup_module.
```

Fix:

```sh
./scripts/build-module.sh
readelf -sW build/linux-4.4.84/drivers/android/binder.ko | grep cleanup_module
TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 scripts/reboot-tv-and-load-build-binder.sh
```

### 14.2 `insmod: File exists`

Cause:

```text
binder is already loaded.
```

If it is unloadable:

```sh
rmmod binder
```

If it is permanent or a process is stuck inside Binder:

```sh
ssh root@192.168.2.121 'sync; reboot'
```

Then load the build KO early.

### 14.3 `Module binder is in use`

Cause:

```text
A Binder user process is still alive, or a test process is stuck inside the kernel.
```

Try:

```sh
killall servicemanager hwservicemanager vndservicemanager mini_servicemgr android_like_fd_passing_service android_like_fd_passing_client 2>/dev/null || true
sleep 1
rmmod binder
```

If that fails, reboot. Do not reuse the old module.

### 14.4 No sidecar binaries

Symptom:

```text
ERROR: missing binary mini_servicemgr
ERROR: missing binary android_like_fd_passing_service
ERROR: missing binary android_like_fd_passing_client
```

Fix:

```sh
./scripts/build-sidecar.sh
```

The deploy script maps:

```text
build/mini_servicemgr_static -> bin/mini_servicemgr
build/android_like_fd_passing_service_static -> bin/android_like_fd_passing_service
build/android_like_fd_passing_client_static -> bin/android_like_fd_passing_client
```

### 14.5 FD test reaches `BR_FAILED_REPLY`

If `fd_debug_stage=0..6`, this can be expected because those stages intentionally reject after proving a step.

For the final path use:

```text
fd_debug_stage=7
```

and expect:

```text
BINDER_FD_SMOKE_TV_OK
```

### 14.6 `target_task_pid=0` or corrupt `comm`

Do not debug this by using more `task_struct` fields. The module build tree and the TV kernel layout are not guaranteed to match.

Use:

```text
target_proc->files
```

not:

```text
target_proc->tsk->files
current->files
```

### 14.7 `source_alloc_ret=-24`

`-24` is `EMFILE`. In early probes it came from an unsuitable allocation range or wrong file table access, not from the final solution.

The validated final path uses:

```text
dirty___alloc_fd(target_proc->files, 0, 1024, O_CLOEXEC)
```

---

## 15. Safety notes

This project loads a custom kernel module into a consumer TV. Expect crashes during development. Keep SSH access, a power-cycle path, and a known-good module.

Recommended discipline:

```text
never run new FD kernel paths without a stage or timeout
prefer unloadable modules for all experiments
refuse to reuse stuck modules
prefer build/ binder.ko over artifacts/
keep the real Android servicemanager path separate from mini servicemanager tests
do not run stage/debug code as a production driver
```

---

## 16. Final validation checklist

Run this before considering a commit ready:

```sh
cd /home/pi/disk/webos-dirty-binder

KCFLAGS="-Wno-error -Wno-error=unused-variable -Wno-error=unused-function" ./scripts/build-module.sh

modinfo -p build/linux-4.4.84/drivers/android/binder.ko | grep fd_debug_stage
readelf -sW build/linux-4.4.84/drivers/android/binder.ko | grep cleanup_module

TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 scripts/reboot-tv-and-load-build-binder.sh

ROUNDS=16 NO_BUILD=1 TV_IP=192.168.2.121 FD_DEBUG_STAGE=7 FD_STAGE_TIMEOUT=40 scripts/run-binder-fd-stage-tv.sh

ssh root@192.168.2.121 '
grep "^binder " /proc/modules
killall servicemanager hwservicemanager vndservicemanager mini_servicemgr android_like_fd_passing_service android_like_fd_passing_client 2>/dev/null || true
sleep 1
rmmod binder
echo "rmmod rc=$?"
grep "^binder " /proc/modules || echo "binder unloaded OK"
'
```

Required markers:

```text
fd_debug_stage:Dirty Binder FD debug stage
cleanup_module
LOAD_BUILD_UNLOADABLE_BINDER_OK
BINDER_FD_SMOKE_TV_OK
BINDER_FD_STAGE7_REAL_FD_OK
rmmod rc=0
binder unloaded OK
```

---

## 17. Git workflow

After applying the final patch:

```sh
git status
git diff --stat
git diff -- scripts/build-module.sh scripts/install-android-usb.sh src/binder_dirty_exports.h README.md
git add README.md scripts src patches configs
git commit -m "Fix Binder FD passing and make dirty binder unloadable"
git push origin main
```

If the commit includes generated build output accidentally, remove it before committing:

```sh
git reset build/ artifacts/*.ko
```

The normal source commit should include scripts, patch injectors, headers, README, and tests, not the local `build/` kernel tree.
