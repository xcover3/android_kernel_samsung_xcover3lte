/*
 * linux/drivers/video/mmp/setup.c
 * Setup interfaces for Marvell Display Controller
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/of_fdt.h>
#include <linux/of.h>
#include <linux/memblock.h>
#include <video/mmp_disp.h>

char panel_type[20];
/*
 * FIXME: the string from cmdline should be like
 * "androidboot.lcd=1080_50"
 */
/* FB memeory reservation, 0x1800000@0x17000000 by default */
static u32 fb_size = 0x1800000;
static u32 fb_addr = 0x17000000;
static u32 buf_num = 3;

#ifdef CONFIG_OF
static int __init mmp_fdt_find_fb_info(unsigned long node, const char *uname,
		int depth, void *data)
{
	__be32 *prop;
	unsigned long len;

	if (!of_flat_dt_is_compatible(node, "marvell,mmp-fb"))
		return 0;

	prop = of_get_flat_dt_prop(node, "marvell,buffer-num", &len);
	if (!prop || (len != sizeof(u32))) {
		pr_err("%s: Can't find buf num property\n", __func__);
		return 0;
	}

	buf_num = be32_to_cpu(prop[0]);

	prop = of_get_flat_dt_prop(node, "marvell,fb-mem", &len);
	if (!prop || (len != sizeof(u32))) {
		pr_err("%s: Can't find fb-mem property\n", __func__);
		return 0;
	}

	fb_addr = be32_to_cpu(prop[0]);

	return 1;
}
#endif

static int __init panel_setup(char *str)
{
	strlcpy(panel_type, str, 20);
#ifdef CONFIG_OF
	if (!of_scan_flat_dt(mmp_fdt_find_fb_info, NULL))
		return 0;
#endif
	if ((!strcmp(str , "1080_50")) || (!strcmp(str , "1080p")))
		fb_size = PAGE_ALIGN(MMP_XALIGN(1080) *
				MMP_YALIGN(1920) * 4 * buf_num);
	else
		fb_size = PAGE_ALIGN(MMP_XALIGN(720) *
				MMP_YALIGN(1280) * 4 * buf_num);

	return 0;
}
early_param("androidboot.lcd", panel_setup);

void __init mmp_reserve_fbmem(void)
{
	BUG_ON(memblock_reserve(fb_addr, fb_size));
	pr_info("FRMEBUFFER reserved memory: 0x%08x@0x%08x\n",
			fb_size, fb_addr);
}

char *mmp_get_paneltype(void)
{
	return panel_type;
}
