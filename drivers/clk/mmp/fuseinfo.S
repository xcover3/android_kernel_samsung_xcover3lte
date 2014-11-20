#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

/*
 *  int smc_get_fuse_info(u64 function_id, void *arg)
 */
ENTRY(smc_get_fuse_info)
	mov	x7, x1
	smc	#0
	str	w1, [x7], #4
	str	w2, [x7], #4
	str	w3, [x7], #4
	str	w4, [x7], #4
	str	w5, [x7], #4
	str	w6, [x7], #4
	ret
ENDPROC(smc_get_fuse_info)
