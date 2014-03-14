#ifndef _ARM64_KEXEC_H
#define _ARM64_KEXEC_H

#if defined(CONFIG_KEXEC)

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

#define KEXEC_CONTROL_PAGE_SIZE	4096
#define KEXEC_ARCH_ARM64   (183 << 16)
#define KEXEC_ARCH KEXEC_ARCH_ARM64

#if !defined(__ASSEMBLY__)

/**
 * crash_setup_regs() - save registers for the panic kernel
 * @newregs: registers are saved here
 * @oldregs: registers to be saved (may be %NULL)
 *
 * Function copies machine registers from @oldregs to @newregs. If @oldregs is
 * %NULL then current registers are stored there.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
				    struct pt_regs *oldregs)
{
	if (oldregs)
		memcpy(newregs, oldregs, sizeof(*newregs));
	else
		pr_warn("%s:%d: Not implemented yet.\n", __func__, __LINE__);
}

/* Function pointer to optional machine-specific reinitialization */
extern void (*kexec_reinit)(void);

#endif /* __ASSEMBLY__ */

#endif /* CONFIG_KEXEC */

#endif /* _ARM64_KEXEC_H */
