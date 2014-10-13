#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

/*
 *  int smc_get_fuse_info(u64 function_id, void *arg)
 */
ENTRY(smc_get_fuse_info)
	mov	x6, x1
	smc	#0
	str	w1, [x6], #4
	str	w2, [x6], #4
	str	w3, [x6], #4
	str	w4, [x6], #4
	str	w5, [x6], #4
	ret
ENDPROC(smc_get_fuse_info)
