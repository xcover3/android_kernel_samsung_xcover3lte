/*
 * linux/include/video/mmp_esd.h
 * Header file for Marvell MMP Display Controller
 *
 * Copyright (C) 2013 Marvell Technology Group Ltd.
 * Authors: Yu Xu <yuxu@marvell.com>
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

#ifndef _MMP_ESD_H_
#define _MMP_ESD_H_

#include <video/mipi_display.h>
#include <linux/delay.h>

static inline void esd_start(struct dsi_esd *esd)
{
	schedule_delayed_work(&esd->work, 5 * HZ);
}

static inline void esd_stop(struct dsi_esd *esd)
{
	cancel_delayed_work_sync(&esd->work);
}

static inline void esd_work(struct work_struct *work)
{
	int status = 0;
	struct dsi_esd *esd =
		(struct dsi_esd *)container_of(work,
					struct dsi_esd, work.work);
	struct mmp_panel *panel =
		(struct mmp_panel *)container_of(esd,
					struct mmp_panel, esd);

	if (esd->esd_check)
		status = esd->esd_check(panel);
	if (status && esd->esd_recover)
		esd->esd_recover(panel);
	esd_start(esd);
}

static inline void esd_panel_recover(struct mmp_path *path)
{
	struct mmp_dsi *dsi = mmp_path_to_dsi(path);
	if (dsi && dsi->set_status)
		dsi->set_status(dsi, MMP_RESET);
}

static inline void esd_init(struct mmp_panel *panel)
{
	struct dsi_esd *esd = &panel->esd;

	INIT_DEFERRABLE_WORK(&esd->work, esd_work);
	esd->esd_check = panel->get_status;
	esd->esd_recover = panel->panel_esd_recover;
}

#endif  /* _MMP_ESD_H_ */
