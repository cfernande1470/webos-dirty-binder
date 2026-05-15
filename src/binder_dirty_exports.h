#ifndef _BINDER_DIRTY_EXPORTS_H
#define _BINDER_DIRTY_EXPORTS_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/err.h>

static unsigned long sym_zap_page_range;
static unsigned long sym_put_files_struct;
static unsigned long sym_get_vm_area;
static unsigned long sym___fd_install;
static unsigned long sym___close_fd;
static unsigned long sym_map_kernel_range_noflush;
static unsigned long sym___lock_task_sighand;
static unsigned long sym_get_files_struct;
static unsigned long sym___alloc_fd;

module_param(sym_zap_page_range, ulong, 0400);
module_param(sym_put_files_struct, ulong, 0400);
module_param(sym_get_vm_area, ulong, 0400);
module_param(sym___fd_install, ulong, 0400);
module_param(sym___close_fd, ulong, 0400);
module_param(sym_map_kernel_range_noflush, ulong, 0400);
module_param(sym___lock_task_sighand, ulong, 0400);
module_param(sym_get_files_struct, ulong, 0400);
module_param(sym___alloc_fd, ulong, 0400);

static inline int dirty_missing(unsigned long addr, const char *name)
{
	if (!addr) {
		pr_err("binder_dirty: missing address for %s\n", name);
		return 1;
	}
	return 0;
}

static inline void dirty_zap_page_range(struct vm_area_struct *vma,
					unsigned long address,
					unsigned long size,
					struct zap_details *details)
{
	typedef void (*fn_t)(struct vm_area_struct *, unsigned long, unsigned long, struct zap_details *);
	if (dirty_missing(sym_zap_page_range, "zap_page_range"))
		return;
	((fn_t)sym_zap_page_range)(vma, address, size, details);
}

static inline void dirty_put_files_struct(struct files_struct *files)
{
	typedef void (*fn_t)(struct files_struct *);
	if (dirty_missing(sym_put_files_struct, "put_files_struct"))
		return;
	((fn_t)sym_put_files_struct)(files);
}

static inline struct vm_struct *dirty_get_vm_area(unsigned long size, unsigned long flags)
{
	typedef struct vm_struct *(*fn_t)(unsigned long, unsigned long);
	if (dirty_missing(sym_get_vm_area, "get_vm_area"))
		return NULL;
	return ((fn_t)sym_get_vm_area)(size, flags);
}

static inline void dirty___fd_install(struct files_struct *files, unsigned int fd, struct file *file)
{
	typedef void (*fn_t)(struct files_struct *, unsigned int, struct file *);
	if (dirty_missing(sym___fd_install, "__fd_install"))
		return;
	((fn_t)sym___fd_install)(files, fd, file);
}

static inline int dirty___close_fd(struct files_struct *files, unsigned int fd)
{
	typedef int (*fn_t)(struct files_struct *, unsigned int);
	if (dirty_missing(sym___close_fd, "__close_fd"))
		return -EINVAL;
	return ((fn_t)sym___close_fd)(files, fd);
}

static inline int dirty_map_kernel_range_noflush(unsigned long addr,
						 unsigned long size,
						 pgprot_t prot,
						 struct page **pages)
{
	typedef int (*fn_t)(unsigned long, unsigned long, pgprot_t, struct page **);
	if (dirty_missing(sym_map_kernel_range_noflush, "map_kernel_range_noflush"))
		return -EINVAL;
	return ((fn_t)sym_map_kernel_range_noflush)(addr, size, prot, pages);
}

static inline struct sighand_struct *dirty___lock_task_sighand(struct task_struct *tsk,
							       unsigned long *flags)
{
	typedef struct sighand_struct *(*fn_t)(struct task_struct *, unsigned long *);
	if (dirty_missing(sym___lock_task_sighand, "__lock_task_sighand"))
		return NULL;
	return ((fn_t)sym___lock_task_sighand)(tsk, flags);
}

static inline struct files_struct *dirty_get_files_struct(struct task_struct *task)
{
	typedef struct files_struct *(*fn_t)(struct task_struct *);
	if (dirty_missing(sym_get_files_struct, "get_files_struct"))
		return NULL;
	return ((fn_t)sym_get_files_struct)(task);
}

static inline int dirty___alloc_fd(struct files_struct *files,
				   unsigned start,
				   unsigned end,
				   unsigned flags)
{
	typedef int (*fn_t)(struct files_struct *, unsigned, unsigned, unsigned);
	if (dirty_missing(sym___alloc_fd, "__alloc_fd"))
		return -EINVAL;
	return ((fn_t)sym___alloc_fd)(files, start, end, flags);
}

static inline int dirty_can_nice(const struct task_struct *p, const int nice)
{
	return 1;
}

static inline int dirty_security_binder_set_context_mgr(struct task_struct *mgr)
{
	return 0;
}

static inline int dirty_security_binder_transaction(struct task_struct *from,
						   struct task_struct *to)
{
	return 0;
}

static inline int dirty_security_binder_transfer_binder(struct task_struct *from,
							struct task_struct *to)
{
	return 0;
}

static inline int dirty_security_binder_transfer_file(struct task_struct *from,
						      struct task_struct *to,
						      struct file *file)
{
	return 0;
}

#endif
