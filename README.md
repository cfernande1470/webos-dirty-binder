# webos-dirty-binder — README_V2

> Milestone: **USB-only Android rootfs + Binder con símbolos + `servicemanager` real de Android 13 funcionando en webOS**  
> Fecha del hito: 2026-05-18  
> Target probado: LG webOS TV con kernel `4.4.84-229.1.kavir.2` aarch64  
> Host de control: NanoPi R3S  
> Objetivo final: ejecutar Android dentro de webOS como una app, sin usar `/media/internal` como almacenamiento principal.

---

## 1. Resumen ejecutivo

Este proyecto intenta llevar un entorno Android funcional a una LG webOS TV rooteada. El objetivo no es simplemente montar un `system.img`, sino acercarnos a una ejecución realista de Android dentro de webOS, con Binder funcionando, `servicemanager` real arrancado, rootfs Android en USB y una futura capa de integración para mostrar Android como una aplicación webOS.

El hito actual es importante porque se ha conseguido algo que llevaba dando vueltas varios días:

```text
El servicemanager real de Android 13 arranca y se queda vivo.
```

No es el `mini_servicemgr` experimental del repo. El binario que arranca es:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

El diagnóstico actual muestra:

```text
== running service managers ==
 5945 ?        00:00:00 servicemanager

== servicemanager test ==
SERVICEMANAGER_ALREADY_RUNNING=YES
```

y el log de instalación confirma:

```text
ANDROID_REAL_SERVICEMANAGER_RUNNING pid=5945
ANDROID_USB_INSTALL_DONE
```

Este hito desbloquea la siguiente fase: `hwbinder`, `hwservicemanager`, linkerconfig, property service y más tarde `zygote`/`system_server`.

---

## 2. Estado actual del proyecto

### 2.1 Funciona

Actualmente funciona lo siguiente:

```text
USB ext4 como almacenamiento principal
Android system.img descargado en USB
Android vendor.img descargado en USB
Montaje de system/vendor/apex/data/cache/proc/sys/dev en rootfs Android
binder.ko cargado en webOS
/dev/binder creado
Binder mmap funcionando
BINDER_SET_CONTEXT_MGR_EXT alcanzado
/system/bin/toybox ejecuta dentro del chroot
/system/bin/servicemanager real de Android 13 arranca y queda vivo
```

El rootfs Android queda en:

```text
/tmp/android-usb/android-rootfs
```

Las imágenes quedan en:

```text
/tmp/android-usb/android-images/system.img
/tmp/android-usb/android-images/vendor.img
```

Los logs y runtime del sidecar quedan en:

```text
/tmp/android-usb/android-sidecar/logs
/tmp/android-usb/android-sidecar/run
```

### 2.2 No funciona todavía

Todavía no funciona:

```text
/dev/hwbinder
/dev/vndbinder
hwservicemanager
vndservicemanager
linkerconfig generado completo
property service Android
Android init real o mini-init equivalente
zygote
system_server
SurfaceFlinger
renderizado Android dentro de una app webOS
input/audio/network Android completos
```

El fallo actual esperado es:

```text
HWSERVICEMANAGER_EXITED_QUICKLY=YES
Binder driver could not be opened. Terminating.
```

La causa no es el USB ni `servicemanager`. La causa es que el módulo Binder actual sólo expone:

```text
/dev/binder
```

y no expone:

```text
/dev/hwbinder
/dev/vndbinder
```

---

## 3. Decisión clave: abandonar `/media/internal`

Inicialmente se usaba `/media/internal` como almacenamiento para parte del sidecar y del entorno Android. Eso no es viable porque en las TVs webOS ese almacenamiento es pequeño y puede llenarse muy rápido.

Se tomó la decisión de mover **todo** lo relacionado con Android al USB:

```text
sidecar
logs
binder.ko copiado a la TV
system.img
vendor.img
descargas temporales
mountpoints
rootfs Android
/data
/cache
```

La ruta lógica estable elegida es:

```text
/tmp/android-usb
```

Aunque `/tmp` no es persistente, el contenido real vive en el USB. `/tmp/android-usb` es sólo el punto de montaje lógico. Esto evita depender de rutas automáticas de webOS como:

```text
/tmp/usb/sda/sda1
```

que pueden variar o cambiar entre arranques.

### 3.1 Por qué USB ext4

Se decidió usar ext4 porque Android necesita semántica UNIX real:

```text
permisos
owners
symlinks
device-like layout
archivos grandes
montajes bind
loop mounts
/data y /cache con permisos normales
```

FAT32, exFAT o NTFS pueden servir para transportar imágenes, pero no son una buena base para un rootfs Android persistente.

---

## 4. Limpieza de scripts

Después del milestone se decidió limpiar el repositorio y quedarse con tres scripts públicos:

```text
scripts/install-android-usb.sh
scripts/tail-android-usb.sh
scripts/diagnose-android-usb.sh
```

Esta simplificación es deliberada.

### 4.1 install

```text
scripts/install-android-usb.sh
```

Responsabilidades:

```text
leer configs/android-usb.env
opcionalmente formatear el USB
montar el USB
localizar o compilar binder.ko
copiar binder.ko a la TV
descargar system.zip/vendor.zip si faltan
extraer system.img/vendor.img
cargar binder.ko con símbolos kernel
crear/fijar /dev/binder
montar rootfs Android
ejecutar test de toybox
arrancar servicemanager real de Android
```

### 4.2 tail

```text
scripts/tail-android-usb.sh
```

Responsabilidad:

```text
hacer tail -F del log de instalación remoto
```

Log principal:

```text
/tmp/android-usb/android-sidecar/logs/android-usb-install.log
```

### 4.3 diagnose

```text
scripts/diagnose-android-usb.sh
```

Responsabilidades:

```text
mostrar kernel
mostrar mounts USB/Android
mostrar dispositivos Binder
mostrar parámetros del módulo binder
mostrar build props Android
mostrar binarios clave
probar getprop
ver si servicemanager está corriendo
probar hwservicemanager
mostrar tail del log de instalación
mostrar dmesg Binder
```

### 4.4 Scripts antiguos eliminables

Se consideran reemplazados u obsoletos:

```text
ensure-android-usb-mounted-tv.sh
format-android-usb-tv.sh
tv-install-android-usb.sh
install-android-to-usb-tv.sh
tail-android-usb-install-tv.sh
diagnose-android-usb-tv.sh
start-real-android-sm-tv.sh
usb-android-status-tv.sh
load-binder-usb-tv.sh
reload-binder-with-symbols-tv.sh
apply-usb-only-migration.sh
apply-usb-safety-guards.sh
```

Algunos scripts de build sí conviene conservar:

```text
scripts/build-module.sh
scripts/build-sidecar.sh
```

`build-module.sh` sigue siendo necesario para compilar `binder.ko`.

`build-sidecar.sh` ya no es obligatorio para la instalación limpia actual. Puede conservarse como herramienta de debug o para futuros tests Binder/FD, pero el instalador principal ya no debe depender de copiar `android_like*`, `mini_servicemgr` u otros binarios estáticos.

---

## 5. Configuración

Archivo principal:

```text
configs/android-usb.env
```

Ejemplo:

```sh
TV_IP="192.168.2.121"
ANDROID_USB_PART="/dev/sda1"
ANDROID_USB_MOUNT="/tmp/android-usb"

ANDROID_SIDE_DIR="/tmp/android-usb/android-sidecar"
ANDROID_IMAGES_DIR="/tmp/android-usb/android-images"
ANDROID_DOWNLOADS_DIR="/tmp/android-usb/android-downloads"
ANDROID_MOUNTS_DIR="/tmp/android-usb/android-mounts"
ANDROID_ROOTFS_DIR="/tmp/android-usb/android-rootfs"
ANDROID_DATA_DIR="/tmp/android-usb/android-data"
ANDROID_CACHE_DIR="/tmp/android-usb/android-cache"

ANDROID_BINDER_KO="/tmp/android-usb/android-sidecar/modules/binder.ko"
REQUIRE_BINDER="1"
START_SERVICEMANAGER="1"

SYSTEM_ZIP_URL="https://sourceforge.net/projects/waydroid/files/images/system/lineage/waydroid_arm64_only/lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip/download"
VENDOR_ZIP_URL="https://sourceforge.net/projects/waydroid/files/images/vendor/waydroid_arm64_only/lineage-20.0-20260403-MAINLINE-waydroid_arm64_only-vendor.zip/download"
```

---

## 6. Compilación desde cero

Para este milestone, lo único imprescindible de compilar es:

```text
binder.ko
```

`build-sidecar.sh` no es obligatorio para instalar Android USB ni para arrancar el `servicemanager` real.

### 6.1 Limpieza segura

No borrar el árbol entero del kernel si ya está preparado. Basta con limpiar artefactos:

```sh
cd /home/pi/disk/webos-dirty-binder

find build -type f \( \
  -name '*.o' -o \
  -name '*.ko' -o \
  -name '*.mod' -o \
  -name '*.mod.c' -o \
  -name '*.cmd' -o \
  -name '*.symvers' -o \
  -name 'modules.order' \
\) -delete 2>/dev/null || true
```

### 6.2 Compilar binder.ko

```sh
chmod +x scripts/*.sh
./scripts/build-module.sh
```

Verificar:

```sh
ls -lh build/linux-4.4.84/drivers/android/binder.ko
modinfo build/linux-4.4.84/drivers/android/binder.ko 2>/dev/null || true
```

### 6.3 No compilar sidecar salvo debug

No hace falta:

```sh
./scripts/build-sidecar.sh
```

Sólo usarlo si se quieren herramientas auxiliares, tests Binder o binarios estáticos para depuración.

---

## 7. Instalación normal

Desde la NanoPi:

```sh
cd /home/pi/disk/webos-dirty-binder
TV_IP=192.168.2.121 ./scripts/install-android-usb.sh
```

Ver progreso:

```sh
TV_IP=192.168.2.121 ./scripts/tail-android-usb.sh
```

Diagnóstico:

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

---

## 8. Instalación formateando USB

Formatear es destructivo y requiere confirmación explícita.

```sh
TV_IP=192.168.2.121 \
ANDROID_USB_PART=/dev/sda1 \
FORMAT_USB=1 \
CONFIRM_FORMAT_ANDROID_USB=YES \
./scripts/install-android-usb.sh
```

Si el formateo falla con:

```text
/dev/sda1 is apparently in use by the system; will not make a filesystem here!
```

significa que webOS o algún loop mount todavía tiene la partición montada. Hay que desmontar en orden:

```text
android-rootfs/dev
android-rootfs/sys
android-rootfs/proc
android-rootfs/cache
android-rootfs/data
android-rootfs/linkerconfig
android-rootfs/apex
android-rootfs/vendor
android-rootfs/system
android-mounts/vendor_raw
android-mounts/system_raw
/tmp/android-usb
/tmp/usb/sda/sda1
```

El instalador debe ser conservador: si queda algo montado, no debe intentar `mkfs`.

---

## 9. Layout del USB

Después de instalar:

```text
/tmp/android-usb/
  android-sidecar/
    bin/
    modules/
      binder.ko
    logs/
      android-usb-install.log
      servicemanager.log
      hwservicemanager.log
    run/
      android-usb-install.pid
      servicemanager.pid

  android-images/
    system.img
    vendor.img

  android-downloads/
    system.zip
    vendor.zip

  android-mounts/
    system_raw/
    vendor_raw/

  android-rootfs/
    system/
    vendor/
    apex/
    data/
    cache/
    proc/
    sys/
    dev/
    linkerconfig/

  android-data/
  android-cache/
```

---

## 10. Android images

Se están usando imágenes Waydroid/Lineage Android 13 arm64-only:

```text
lineage-20.0-20260403-VANILLA-waydroid_arm64_only-system.zip
lineage-20.0-20260403-MAINLINE-waydroid_arm64_only-vendor.zip
```

El motivo de usar Waydroid/Lineage es pragmático:

```text
imágenes Android ya separadas en system/vendor
formato manejable
Android 13 moderno
arm64-only compatible con el target aarch64
suficientemente estándar para probar Binder/servicemanager
```

El instalador descarga los zip, extrae:

```text
system.img
vendor.img
```

y monta ambas imágenes en loop:

```text
/tmp/android-usb/android-mounts/system_raw
/tmp/android-usb/android-mounts/vendor_raw
```

Luego crea el rootfs Android mediante bind mounts.

---

## 11. El problema de servicemanager

### 11.1 Síntoma inicial

Durante días el `servicemanager` no arrancaba. Primero parecía que el problema podía estar en:

```text
el USB
el chroot
linkerconfig
FD passing
servicemanager incompatible
el mini service manager
permisos de /dev/binder
```

Pero los logs fueron reduciendo el problema.

El primer bloqueo era:

```text
Binder driver '/dev/binder' could not be opened.
Opening '/dev/binder' failed: No such file or directory
```

Eso significaba que el instalador no estaba cargando Binder ni creando `/dev/binder`.

Se corrigió añadiendo carga de `binder.ko` y creación de `/dev/binder`.

### 11.2 Segundo bloqueo: Binder existía pero mmap fallaba

Después de cargar Binder apareció:

```text
Binder driver '/dev/binder' could not be opened.
Using /dev/binder failed: unable to mmap transaction memory.
```

y en `dmesg`:

```text
binder_dirty: missing address for get_vm_area
binder_mmap ... get_vm_area failed -12
```

Esto fue el punto clave.

El módulo `binder.ko` cargaba, pero estaba cargando con símbolos internos a cero:

```text
sym_get_vm_area=0
sym_map_kernel_range_noflush=0
sym_zap_page_range=0
sym___alloc_fd=0
sym___fd_install=0
sym___close_fd=0
sym_get_files_struct=0
sym_put_files_struct=0
sym___lock_task_sighand=0
```

El driver Binder dirty necesita esos símbolos porque está adaptándose a un kernel webOS 4.4 cerrado/limitado, y algunas APIs internas necesarias no están exportadas de forma normal para módulos.

### 11.3 Descubrimiento: /proc/kallsyms tenía las direcciones

Se comprobó en la TV:

```sh
ssh root@192.168.2.121 'sh -s' <<'TVSH'
set -u

echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true

for s in \
  get_vm_area \
  map_kernel_range_noflush \
  zap_page_range \
  __alloc_fd \
  __fd_install \
  __close_fd \
  get_files_struct \
  put_files_struct \
  __lock_task_sighand
do
  echo "== $s =="
  grep -w "$s" /proc/kallsyms | head -n 5 || true
done
TVSH
```

Resultado observado:

```text
get_vm_area              ffffffc0001a28c0
map_kernel_range_noflush ffffffc0001a2878
zap_page_range           ffffffc000194da8
__alloc_fd               ffffffc0001e3230
__fd_install             ffffffc0001e3578
__close_fd               ffffffc0001e35c0
get_files_struct         ffffffc0001e3028
put_files_struct         ffffffc0001e3078
__lock_task_sighand      ffffffc0000b3290
```

Por tanto no hacía falta `System.map` externo. El propio kernel en ejecución exponía lo necesario.

### 11.4 Solución

El instalador ahora resuelve esos símbolos dinámicamente:

```sh
ksym() {
  awk -v n="$1" '$3 == n && $1 != "0000000000000000" { print "0x"$1; exit }' /proc/kallsyms
}
```

y carga Binder así:

```sh
insmod "$ANDROID_BINDER_KO" \
  sym_get_vm_area="$GET_VM_AREA" \
  sym_map_kernel_range_noflush="$MAP_KERNEL_RANGE_NOFLUSH" \
  sym_zap_page_range="$ZAP_PAGE_RANGE" \
  sym___alloc_fd="$ALLOC_FD" \
  sym___fd_install="$FD_INSTALL" \
  sym___close_fd="$CLOSE_FD" \
  sym_get_files_struct="$GET_FILES_STRUCT" \
  sym_put_files_struct="$PUT_FILES_STRUCT" \
  sym___lock_task_sighand="$LOCK_TASK_SIGHAND"
```

En el script real esto se construye como lista dinámica de parámetros.

### 11.5 Resultado

Después de cargar Binder con símbolos:

```text
sym_get_vm_area != 0
sym___alloc_fd != 0
sym___fd_install != 0
sym___close_fd != 0
```

`servicemanager` deja de morir en `mmap()`.

El log de instalación muestra:

```text
ANDROID_BINDER_SYM sym_get_vm_area=0xffffffc0001a28c0
ANDROID_BINDER_SYM sym_map_kernel_range_noflush=0xffffffc0001a2878
ANDROID_BINDER_SYM sym_zap_page_range=0xffffffc000194da8
ANDROID_BINDER_SYM sym___alloc_fd=0xffffffc0001e3230
ANDROID_BINDER_SYM sym___fd_install=0xffffffc0001e3578
ANDROID_BINDER_SYM sym___close_fd=0xffffffc0001e35c0
ANDROID_BINDER_SYM sym_get_files_struct=0xffffffc0001e3028
ANDROID_BINDER_SYM sym_put_files_struct=0xffffffc0001e3078
ANDROID_BINDER_SYM sym___lock_task_sighand=0xffffffc0000b3290
ANDROID_BINDER_READY
```

y finalmente:

```text
ANDROID_REAL_SERVICEMANAGER_RUNNING pid=5945
ANDROID_USB_INSTALL_DONE
```

---

## 12. Por qué sabemos que es el servicemanager real

Porque el binario verificado por diagnóstico está en:

```text
/tmp/android-usb/android-rootfs/system/bin/servicemanager
```

El diagnóstico muestra:

```text
--- /system/bin/servicemanager
-rwxr-xr-x    1 root     2000         67824 Apr  3 07:37 /tmp/android-usb/android-rootfs/system/bin/servicemanager
```

y el proceso vivo es:

```text
5945 ?        00:00:00 servicemanager
```

No se está ejecutando el `mini_servicemgr` del repo. De hecho, el flujo limpio ya no necesita copiar binarios `android_like*` ni shims antiguos para arrancar el manager real.

Además, en `dmesg` aparece:

```text
DIRTY_BINDER_IOCTL_COMPAT_V0 set context mgr ext type=0x0 flags=0x0
```

Eso confirma que el binario real está llegando al punto crítico de registrarse como Binder context manager usando el ioctl moderno/ext.

---

## 13. Binder y FD passing

Al principio se sospechaba que el problema de FD y el problema de `servicemanager` podían estar relacionados. La sospecha era correcta, pero el orden era importante.

`servicemanager` no estaba fallando por FD passing todavía. Fallaba antes, en Binder `mmap()`, porque `sym_get_vm_area` estaba a cero.

Sin embargo, los símbolos de FD también son importantes:

```text
sym___alloc_fd
sym___fd_install
sym___close_fd
sym_get_files_struct
sym_put_files_struct
```

Si esos símbolos están a cero, el paso de file descriptors por Binder probablemente fallará más adelante. Ahora están cargando correctamente.

Esto no prueba que FD passing funcione al 100 %, pero sí elimina el fallo obvio anterior.

Milestone futuro:

```text
crear test cliente/servidor Binder que envíe un FD real
validar BINDER_TYPE_FD / BINDER_TYPE_FDA
validar cierre/instalación correcta del FD en proceso destino
```

---

## 14. Por qué no se debe falsificar hwbinder

El módulo actual sólo registra:

```text
53 binder
/dev/binder
```

El log indica:

```text
binder: unknown parameter 'devices' ignored
```

Eso significa que este módulo no soporta:

```text
devices=binder,hwbinder,vndbinder
```

Se podría intentar crear:

```sh
ln -s /dev/binder /dev/hwbinder
```

o un `mknod` con el mismo major/minor, pero es una mala idea.

Android moderno separa dominios Binder:

```text
/dev/binder      -> framework servicemanager
/dev/hwbinder    -> HAL/hwservicemanager
/dev/vndbinder   -> vendor binder domain
```

`servicemanager` y `hwservicemanager` son context managers distintos. Si ambos usan el mismo dispositivo Binder, compiten por el mismo context manager y el resultado será incorrecto.

La solución real es parchear o reemplazar `binder.ko` para registrar múltiples dispositivos Binder independientes.

---

## 15. Linkerconfig

El warning actual es:

```text
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

De momento no bloquea:

```text
/system/bin/toybox
/system/bin/servicemanager
```

Pero sí será importante para fases posteriores.

Android moderno usa `linkerconfig` para generar configuración dinámica del linker basada en APEX, particiones y namespaces. Para `zygote`, `system_server`, HALs y servicios más complejos, habrá que resolverlo.

El rootfs ya incluye:

```text
/apex/com.android.runtime/bin/linkerconfig
/system/bin/linkerconfig -> /apex/com.android.runtime/bin/linkerconfig
```

y se crea el directorio:

```text
/tmp/android-usb/android-rootfs/linkerconfig
```

Milestone futuro:

```text
generar /linkerconfig/ld.config.txt correctamente
montar/bindear linkerconfig de forma compatible
proporcionar propiedades mínimas que linkerconfig espera
```

---

## 16. Property service y getprop

`getprop` actualmente muestra warning y no devuelve versión:

```text
chroot getprop
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

Esto no significa que Android esté roto. Significa que aún no está corriendo el property service de Android.

El binario `getprop` existe:

```text
/system/bin/getprop -> toolbox
```

pero sin `init`/property service no tiene un entorno Android completo del que leer propiedades.

Milestone futuro:

```text
implementar property service mínimo
o arrancar una parte controlada de Android init
o inyectar propiedades estáticas suficientes para linkerconfig y servicios base
```

---

## 17. SELinux, namespaces y cgroups

Este milestone no ha resuelto todavía:

```text
SELinux Android
cgroups Android
pid namespaces
mount namespaces
net namespaces
binderfs
ashmem/memfd strategy
```

Para arrancar `servicemanager` no han sido el primer bloqueo. Para Android completo sí lo serán.

Decisión actual:

```text
no resolver SELinux antes de Binder básico
no resolver zygote antes de hwbinder/linkerconfig/property service
no intentar system_server hasta tener servicios base Android coherentes
```

---

## 18. Milestones siguientes hacia Android como app webOS

### Milestone 1 — Binder multi-device

Objetivo:

```text
/dev/binder
/dev/hwbinder
/dev/vndbinder
```

Tareas:

```text
parchear binder.ko para registrar varios misc devices
mantener context managers separados
hacer que devices=binder,hwbinder,vndbinder no sea ignorado
probar servicemanager en /dev/binder
probar hwservicemanager en /dev/hwbinder
probar vndbinder si la imagen lo requiere
```

Criterio de éxito:

```text
servicemanager RUNNING
hwservicemanager RUNNING
no symlinks falsos
dmesg sin choques de context manager
```

### Milestone 2 — FD passing test

Objetivo:

```text
validar paso de file descriptors por Binder
```

Tareas:

```text
crear test Binder cliente/servidor mínimo
enviar pipe o /dev/null por Binder
verificar que el proceso destino recibe un FD válido
verificar cierre y lifecycle
```

Criterio de éxito:

```text
BINDER_TYPE_FD funciona
BINDER_TYPE_FDA si aplica funciona
sin leaks obvios
sin crashes kernel
```

### Milestone 3 — linkerconfig mínimo

Objetivo:

```text
eliminar warning /linkerconfig/ld.config.txt
```

Tareas:

```text
ejecutar linkerconfig dentro del rootfs
proporcionar propiedades necesarias
montar /linkerconfig correctamente
validar binarios dinámicos más complejos
```

Criterio de éxito:

```text
getprop/toybox/servicemanager sin warning de linkerconfig
ld.config.txt generado
```

### Milestone 4 — property service mínimo

Objetivo:

```text
getprop funcional
```

Tareas:

```text
arrancar property service Android o equivalente mínimo
preparar default.prop/build.prop/vendor props
exponer socket/property area esperada
```

Criterio de éxito:

```text
getprop ro.build.version.release -> 13
servicios base pueden leer propiedades
```

### Milestone 5 — init strategy

Objetivo:

```text
decidir si arrancar Android init real o un mini-init controlado
```

Opciones:

```text
Android init real dentro de entorno controlado
mini-init propio que arranque sólo servicios necesarios
modelo híbrido
```

Decisión probable:

```text
mini-init primero para mantener control
Android init real después si el entorno kernel/userspace lo permite
```

### Milestone 6 — HAL y hwservicemanager

Objetivo:

```text
HAL layer mínima viva
```

Tareas:

```text
hwservicemanager funcionando
servicios HAL mínimos
revisar vendor.img
decidir qué HALs son necesarias y cuáles se pueden stubear
```

Criterio de éxito:

```text
lshal o equivalente muestra servicios básicos
hwservicemanager estable
```

### Milestone 7 — zygote

Objetivo:

```text
arrancar zygote
```

Requisitos previos:

```text
Binder base
hwbinder si requerido
linkerconfig
property service
cgroups/namespaces mínimos
SELinux strategy
filesystem Android coherente
```

Criterio de éxito:

```text
zygote64 vivo
app_process ejecutando
sin crash inmediato por linker/property/cgroup
```

### Milestone 8 — system_server

Objetivo:

```text
system_server vivo
```

Este es uno de los hitos más difíciles.

Tareas:

```text
resolver servicios Java framework
resolver permisos
resolver sockets y dirs runtime
resolver binder calls iniciales
stubear hardware no disponible
```

Criterio de éxito:

```text
system_server queda vivo más de 30-60s
servicios framework básicos registrados
```

### Milestone 9 — gráficos

Objetivo:

```text
renderizar Android dentro de webOS
```

Opciones posibles:

```text
SurfaceFlinger + backend adaptado
render headless + streaming a app webOS
Wayland/WAM bridge si viable
EGL/GLES directo si las librerías de la TV permiten
framebuffer/texture bridge experimental
```

Decisión probable inicial:

```text
empezar con render headless o buffer bridge controlado
después integrar con una app webOS
```

### Milestone 10 — app webOS wrapper

Objetivo:

```text
Android visible como aplicación webOS
```

Tareas:

```text
crear app webOS launcher
crear servicio nativo o bridge que arranque/paré Android sidecar
mostrar superficie Android
inyectar input remoto/teclado/ratón
manejar lifecycle pause/resume/stop
logs y watchdog
```

Criterio de éxito:

```text
se abre una app webOS
la app muestra la UI Android o una app Android concreta
el mando/control remoto envía input
se puede cerrar limpiamente
```

### Milestone 11 — empaquetado IPK

Objetivo:

```text
instalación reproducible desde webOS
```

Tareas:

```text
empaquetar scripts
detectar USB
instalar binder.ko
descargar imágenes
crear icono/app webOS
crear logs visibles
fallback/recovery
```

---

## 19. Comandos útiles actuales

### Instalar

```sh
TV_IP=192.168.2.121 ./scripts/install-android-usb.sh
```

### Ver progreso

```sh
TV_IP=192.168.2.121 ./scripts/tail-android-usb.sh
```

### Diagnosticar

```sh
TV_IP=192.168.2.121 ./scripts/diagnose-android-usb.sh
```

### Formatear e instalar

```sh
TV_IP=192.168.2.121 \
ANDROID_USB_PART=/dev/sda1 \
FORMAT_USB=1 \
CONFIRM_FORMAT_ANDROID_USB=YES \
./scripts/install-android-usb.sh
```

### Comprobar símbolos Binder en la TV

```sh
ssh root@192.168.2.121 'sh -s' <<'TVSH'
set -u
echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true

for s in \
  get_vm_area \
  map_kernel_range_noflush \
  zap_page_range \
  __alloc_fd \
  __fd_install \
  __close_fd \
  get_files_struct \
  put_files_struct \
  __lock_task_sighand
do
  echo "== $s =="
  grep -w "$s" /proc/kallsyms | head -n 5 || true
done
TVSH
```

---

## 20. Estado validado por diagnóstico

Último estado validado:

```text
/dev/sda1 /tmp/android-usb ext4 rw
system_raw mounted
vendor_raw mounted
android-rootfs/system mounted
android-rootfs/vendor mounted
android-rootfs/apex mounted
android-rootfs/data ext4 rw
android-rootfs/cache ext4 rw
proc mounted
sysfs mounted
devtmpfs bind mounted
```

Binder:

```text
53 binder
/dev/binder exists
binder module loaded
sym_get_vm_area != 0
sym___alloc_fd != 0
sym___fd_install != 0
sym___close_fd != 0
```

Android:

```text
ro.build.version.release=13
/system/bin/servicemanager exists
/system/bin/hwservicemanager exists
/system/bin/toybox exists
```

Servicios:

```text
servicemanager running
hwservicemanager fails due to missing hwbinder
```

Instalador:

```text
ANDROID_BINDER_READY
ANDROID_USB_ROOTFS_READY
ANDROID_USB_TOYBOX_OK
ANDROID_REAL_SERVICEMANAGER_RUNNING
ANDROID_USB_INSTALL_DONE
```

---

## 21. Cómo preparar commit y push

Después de copiar este archivo como `README.md`:

```sh
cp README_V2.md README.md
```

Revisar:

```sh
git status --short
git diff -- README.md configs scripts
```

Añadir:

```sh
git add README.md configs/android-usb.env
git add scripts/install-android-usb.sh scripts/tail-android-usb.sh scripts/diagnose-android-usb.sh
git add -u scripts
```

Commit:

```sh
git commit -m "usb-only android milestone with real servicemanager"
```

Push:

```sh
git push origin main
```

---

## 22. Lecciones aprendidas

### 22.1 El problema no era el USB

El USB fue necesario para espacio y persistencia, pero no era la causa del fallo de `servicemanager`.

### 22.2 El problema no era el binario de servicemanager

El binario Android 13 era capaz de arrancar. Lo impedía Binder.

### 22.3 El primer fallo Binder era simple

No existía `/dev/binder`.

### 22.4 El segundo fallo Binder era sutil

Existía `/dev/binder`, pero Binder no podía hacer `mmap()` porque `get_vm_area` estaba a cero.

### 22.5 `/proc/kallsyms` fue la pieza clave

No hizo falta `System.map`. La TV exponía las direcciones necesarias.

### 22.6 No hace falta mini_servicemgr para este hito

El mini servicemanager fue útil como concepto/debug, pero el milestone real es que Android ejecuta su propio `servicemanager`.

### 22.7 No hay que falsificar hwbinder

El siguiente obstáculo debe resolverse correctamente en el módulo Binder, no con symlinks.

### 22.8 El instalador debe ser pequeño

Tres scripts son suficientes:

```text
install
tail
diagnose
```

Esto hace el proyecto más fácil de usar, revisar y pushear a `main`.

---

## 23. Referencias técnicas

- Android Binder overview: https://source.android.com/docs/core/architecture/ipc/binder-overview
- Android HIDL Binder IPC and binder devices: https://source.android.com/docs/core/architecture/hidl/binder-ipc
- Waydroid custom images: https://docs.waydro.id/faq/using-custom-waydroid-images
- Waydroid Lineage image build notes: https://docs.waydro.id/development/compile-waydroid-lineage-os-based-images

---

## 24. Próximo hito recomendado

El próximo hito debería ser:

```text
binder.ko multi-device: /dev/binder + /dev/hwbinder + /dev/vndbinder
```

No avanzar a `zygote` todavía. El orden más sano es:

```text
1. Binder multi-device
2. hwservicemanager vivo
3. FD passing test real
4. linkerconfig
5. property service
6. init/mini-init
7. zygote
8. system_server
9. render Android dentro de app webOS
```

Este milestone actual es bueno porque ya tenemos la prueba de vida más importante de la capa Binder framework: **el servicemanager real de Android 13 queda vivo dentro del rootfs Android montado en USB sobre webOS**.
