#ifndef _DT_BINDINGS_USB_MV_USB_PHY_H
#define _DT_BINDINGS_USB_MV_USB_PHY_H

/*
 * PHY revision: For those has small difference with default setting.
 * bit [15..8]: represent PHY IP as below:
 *     PHY_55LP	0x5500,
 *     PHY_40LP	0x4000,
 *     PHY_28LP	0x2800,
 */
#define REV_PXA168     0x5500
#define REV_PXA910     0x5501

#endif
