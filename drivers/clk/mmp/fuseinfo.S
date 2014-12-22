#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

/*
 *  int smc_get_fuse_info(u64 function_id, void *arg)
 */
ENTRY(smc_get_fuse_info)
	sub sp, sp, #16
	str x19, [sp]
	mov	x19, x1
	smc	#0
	str	w1, [x19], #4
	str	w2, [x19], #4
	str	w3, [x19], #4
	str	w4, [x19], #4
	str	w5, [x19], #4
	str	w6, [x19], #4
	str	w7, [x19], #4
	ldr x19, [sp]
	add sp, sp, #16
	ret
ENDPROC(smc_get_fuse_info)
