/*
 * drivers/uio/uio_codaL.c
 *
 * chip&media codaL UIO driver
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/uio_codaL.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#ifdef CONFIG_ARM_SMMU
#include <linux/iommu.h>
#include <asm/dma-iommu.h>
extern struct dma_iommu_mapping *pxa_ion_iommu_mapping;
#endif

#define VDEC_WORKING_BUFFER_SIZE	SZ_1M
#define UIO_CODAL_VERSION		"build-001"

/* HW capability is 16, only use 10 instance to reduce memory requirements. */
#define MAX_NUM_VPUSLOT	10
#define MAX_NUM_FILEHANDLE 10

#define BIT_INT_CLEAR		(0xC)
#define BIT_BUSY_FLAG		(0x160)
#define BIT_INT_ENABLE		(0x170)
#define MJPEG_PIC_STATUS_REG	(0x804)

#define CODAL_TYPE_VPU		(0)
#define CODAL_TYPE_JPU		(1)

struct uio_codaL_dev {
	struct uio_info uio_info;
	void *reg_base;
	struct clk *fclk;
	struct clk *aclk;
	struct mutex mutex;	/* used to protect open/release */
	int power_status;       /* 0-off 1-on */
	int clk_status;
	int filehandle_ins_num;
	int firmware_download;
	struct semaphore sema;
	unsigned int coda_features;
	unsigned int jpu_status;
	unsigned int codaL_type;
	struct device *dev;
};

/*
 * use following data structure to assicate fd and codec instance,
 * one fd corresponding to one codec instance
*/
struct codaL_instance {
	int occupied;
	int gotsemaphore;
	int vpuslotidx; /* the item idex in fd_codecinst_arr[]*/
	int slotusing;
	unsigned int codectypeid;
};
static struct codaL_instance fd_codecinst_arr[MAX_NUM_VPUSLOT];

struct vpu_info {
	unsigned int off;
#ifdef CONFIG_COMPAT
	compat_uptr_t value;
#else
	void *value;
#endif
};

static inline unsigned long compat_copy_to_user(unsigned long to,
		const void *from, unsigned long n)
{
#ifdef CONFIG_COMPAT
		return copy_to_user(compat_ptr(to), from, n);
#else
		return copy_to_user((void __user *)to, from, n);
#endif
}

static int codaL_power_on(struct uio_codaL_dev *cdev)
{
	if (cdev->power_status == 1)
		return 0;

#ifdef CONFIG_MMP_PM_DOMAIN_COMMON
	pm_runtime_get_sync(cdev->dev);
#endif
	cdev->power_status = 1;

	return 0;
}

static int codaL_clk_on(struct uio_codaL_dev *cdev)
{
	if (cdev->clk_status != 0)
		return 0;

	clk_prepare_enable(cdev->fclk);
	clk_prepare_enable(cdev->aclk);
	cdev->clk_status = 1;

	return 0;
}

static int codaL_power_off(struct uio_codaL_dev *cdev)
{
	if (cdev->power_status == 0)
		return 0;

#ifdef CONFIG_MMP_PM_DOMAIN_COMMON
	pm_runtime_put_sync(cdev->dev);
#endif
	cdev->power_status = 0;

	return 0;
}

static int codaL_clk_off(struct uio_codaL_dev *cdev)
{
	if (cdev->clk_status == 0)
		return 0;

	clk_disable_unprepare(cdev->aclk);
	clk_disable_unprepare(cdev->fclk);
	cdev->clk_status = 0;

	return 0;
}

static int active_codaL(struct uio_codaL_dev *cdev, int on)
{
	int i, ret;

	if (on) {
		ret = codaL_power_on(cdev);
		if (ret)
			return -1;

		ret = codaL_clk_on(cdev);
		if (ret)
			return -1;

		sema_init(&cdev->sema, 1);
	} else {
		ret = codaL_clk_off(cdev);
		if (ret)
			return -1;

		ret = codaL_power_off(cdev);
		if (ret)
			return -1;
	}

	cdev->firmware_download = 0;

	/*clear fd_codecinst_arr */
	for (i = 0; i < MAX_NUM_VPUSLOT; i++) {
		fd_codecinst_arr[i].occupied = 0;
		fd_codecinst_arr[i].slotusing = 0;
		fd_codecinst_arr[i].codectypeid = 0;
	}

	return 0;
}

static void codaL_cleanup(struct uio_codaL_dev *cdev)
{
	pr_info("%s: process was killed abnormal\n", __func__);
	codaL_clk_on(cdev);
	__raw_writel(0x0, cdev->reg_base + BIT_INT_ENABLE);
	__raw_writel(0x1, cdev->reg_base + BIT_INT_CLEAR);
	codaL_clk_off(cdev);
}


static int __maybe_unused codaL_abnormal_stop(struct uio_codaL_dev *cdev)
{
	int i;
	pr_info("%s: process was killed abnormal\n", __func__);
	/*
	 *currently, we couldn't do other thing except waiting
	 *for CNM VPU stop by itself
	 */
	for (i = 0; i < 6; i++) {
		msleep(5);
		if (0 == __raw_readl(cdev->reg_base + BIT_BUSY_FLAG))
			return 0;
	}
	return -1;
}

static int codaL_open(struct uio_info *info, struct inode *inode,
			void *file_priv)
{
	struct uio_codaL_dev *cdev;
	struct uio_listener *listener = file_priv;
	int ret = 0, i;

	pr_debug("Enter codaL_open()\n");
	cdev = (struct uio_codaL_dev *)info->priv;
	mutex_lock(&cdev->mutex);
	if (cdev->filehandle_ins_num < MAX_NUM_FILEHANDLE)
		cdev->filehandle_ins_num++;
	else {
		pr_info(KERN_ERR "current codaL cannot accept more file handle instance!!\n");
		ret = -EACCES;
		goto out;
	}

	if (cdev->filehandle_ins_num == 1) {
		if (0 != active_codaL(cdev, 1)) {
			cdev->filehandle_ins_num = 0;
			ret = -EACCES;
			goto out;
		}
	}

	for (i = 0; i < MAX_NUM_VPUSLOT; i++) {
		if (fd_codecinst_arr[i].occupied == 0) {
			listener->extend = &fd_codecinst_arr[i];
			fd_codecinst_arr[i].occupied = 1;
			fd_codecinst_arr[i].gotsemaphore = 0;
			break;
		}
	}
	if (i == MAX_NUM_VPUSLOT) {
		cdev->filehandle_ins_num--;
		pr_info(KERN_ERR "couldn't found any idle seat for new fd!!\n");
		ret = -EACCES;
	}

out:
	mutex_unlock(&cdev->mutex);
	pr_debug("Leave codaL_open(), ret = %d\n", ret);
	return ret;
}

static int codaL_release(struct uio_info *info, struct inode *inode,
			void *file_priv)
{
	struct uio_codaL_dev *cdev;
	struct uio_listener *listener = file_priv;
	struct codaL_instance *pinst;
	int i, ret = 0;

	pr_debug("Enter codaL_release()\n");
	cdev = (struct uio_codaL_dev *)info->priv;
	pinst = (struct codaL_instance *)listener->extend;
	mutex_lock(&cdev->mutex);

	/*
	 * if this codec instance got semaphore, it means this codec instance
	 * want VPU to do something, and other codec instance will wait.
	 * If process is killed before VPU finished working for this codec
	 * instance and instance release semaphore, we need do something.
	 */
	if (pinst->gotsemaphore) {
		if (cdev->clk_status == 1) {
			for (i = 0; i < 30; i++) {
				msleep(10);
				if (0 == __raw_readl(cdev->reg_base + BIT_BUSY_FLAG)) {
					/*
					 * the additional sleep is to
					 *  ensure interrupt is handled,
					 *  as we are not sure which occur
					 *  at first: between BUSY->0 and
					 *  interrupt.
					 */
					msleep(10);
					break;
				}
			}
			if (i == 30)
				pr_info("%s: VPU could not stop, will force clean\n", __func__);
			codaL_cleanup(cdev);
		}
		up(&cdev->sema);
		pinst->gotsemaphore = 0;
	}

	if (pinst->slotusing)
		pinst->slotusing = 0;
	pinst->occupied = 0;
	pinst->codectypeid = 0;
	listener->extend = NULL;

	if (cdev->filehandle_ins_num > 0) {
		cdev->filehandle_ins_num--;
		if (cdev->filehandle_ins_num == 0) {
			if (0 != active_codaL(cdev, 0))
				ret = -EFAULT;
		}
	}
	mutex_unlock(&cdev->mutex);
	pr_debug("Leave codaL_release(), ret = %d\n", ret);
	return ret;
}

static irqreturn_t codaL_irq_handler(int irq, struct uio_info *dev_info)
{
	struct uio_codaL_dev *cdev = dev_info->priv;
	if (cdev->codaL_type == CODAL_TYPE_VPU) {
		/* clear the interrupt */
		__raw_writel(0x1, cdev->reg_base + BIT_INT_CLEAR);
	} else if (cdev->codaL_type == CODAL_TYPE_JPU) {
		cdev->jpu_status = __raw_readl(cdev->reg_base + MJPEG_PIC_STATUS_REG);
		if (cdev->jpu_status)
			__raw_writel(cdev->jpu_status, cdev->reg_base + MJPEG_PIC_STATUS_REG);
	} else
		pr_err("codaL: error device type!\n");

	return IRQ_HANDLED;
}

static int codaL_ioctl(struct uio_info *info, unsigned int cmd,
			unsigned long arg, void *file_priv)
{
	unsigned int value;
	int ret, idx;
	struct vpu_info vpu_info;
	void __user *argp = (void __user *)arg;
	struct uio_codaL_dev *cdev = info->priv;
	struct codaL_instance *pinst;
	struct uio_listener *listener = file_priv;
	pinst = (struct codaL_instance *)listener->extend;

	switch (cmd) {
	case CODAL_POWER_ON:
		mutex_lock(&cdev->mutex);
		ret = codaL_power_on(cdev);
		if (0 != ret) {
			mutex_unlock(&cdev->mutex);
			return -EFAULT;
		}
		mutex_unlock(&cdev->mutex);
		break;
	case CODAL_POWER_OFF:
		mutex_lock(&cdev->mutex);
		ret = codaL_power_off(cdev);
		if (0 != ret) {
			mutex_unlock(&cdev->mutex);
			return -EFAULT;
		}
		mutex_unlock(&cdev->mutex);
		break;
	case CODAL_CLK_ON:
		mutex_lock(&cdev->mutex);
		ret = codaL_clk_on(cdev);
		if (0 != ret) {
			mutex_unlock(&cdev->mutex);
			return -EFAULT;
		}
		mutex_unlock(&cdev->mutex);
		break;
	case CODAL_CLK_OFF:
		mutex_lock(&cdev->mutex);
		ret = codaL_clk_off(cdev);
		if (0 != ret) {
			mutex_unlock(&cdev->mutex);
			return -EFAULT;
		}
		mutex_unlock(&cdev->mutex);
		break;
	case CODAL_LOCK:
		if (copy_from_user(&vpu_info, argp, sizeof(struct vpu_info)))
			return -EFAULT;
		if (pinst->gotsemaphore == 0) {
			value = down_timeout(&cdev->sema,
					msecs_to_jiffies(vpu_info.off));
			if (value == 0)
				pinst->gotsemaphore = 1;
		} else
			value = 0;
		if (compat_copy_to_user((unsigned long)vpu_info.value, &value,
				sizeof(unsigned int)))
			return -EFAULT;
		break;
	case CODAL_UNLOCK:
		if (pinst->gotsemaphore) {
			up(&cdev->sema);
			pinst->gotsemaphore = 0;
		}
		break;
	case CODAL_GETSET_INFO:
		if (copy_from_user(&vpu_info, argp, sizeof(struct vpu_info)))
			return -EFAULT;
		switch (vpu_info.off) {
		case 0:
			/* 0 get firmware_download */
			if (compat_copy_to_user((unsigned long)vpu_info.value,
				&cdev->firmware_download, sizeof(int)))
				return -EFAULT;
			break;
		case 1:
			/* 1 set firmware_download */
			cdev->firmware_download = 1;
			break;
		case 2:
			/* 2 get vpu slot */
			if (pinst->slotusing == 0 && pinst->vpuslotidx
					< MAX_NUM_VPUSLOT)
				idx = pinst->vpuslotidx;
			else
				idx = -1;
			if (compat_copy_to_user((unsigned long)vpu_info.value,
					&idx, sizeof(idx)))
				return -EFAULT;
			pinst->slotusing = 1;
			break;
		case 3:
			/* 3 release vpu slot */
			if (pinst->vpuslotidx < MAX_NUM_VPUSLOT)
				pinst->slotusing = 0;
			break;
		case 4:
			if (compat_copy_to_user((unsigned long)vpu_info.value,
				&cdev->coda_features,
					sizeof(cdev->coda_features)))
				return -EFAULT;
			break;
		case 6:
			pinst->codectypeid = (unsigned int)(vpu_info.value);
			break;
		default:
			return -EFAULT;
		}
		break;
	case CODAL_GET_JPU_STATUS:
		if (compat_copy_to_user((unsigned long)argp, &cdev->jpu_status, sizeof(cdev->jpu_status))) {
			pr_err("%s, copy to user error!\n", __func__);
			ret = -EFAULT;
		}
		break;
	default:
		return -EFAULT;
	}

	return 0;
}

static int codaL_probe(struct platform_device *pdev)
{
	struct resource *res, *sram_res;
	struct uio_codaL_dev *cdev;
	int i, irq_func, ret = 0;
	int sram_internal, nv21_support, seconddaxi_none = SECONDAXI_SUPPORT;
	dma_addr_t mem_dma_addr;
	void *mem_vir_addr;
	int codaL_type;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no memory resources given\n");
		return -ENODEV;
	}

	sram_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!sram_res)
		dev_info(&pdev->dev, "no sram resources given\n");

	irq_func = platform_get_irq(pdev, 0);
	if (irq_func < 0) {
		dev_err(&pdev->dev, "missing irq resource in interrupt mode\n");
		return -ENODEV;
	}

	cdev = devm_kzalloc(&pdev->dev, sizeof(*cdev), GFP_KERNEL);
	if (!cdev) {
		dev_err(&pdev->dev, "uio_codaL_dev: out of memory\n");
		return -ENOMEM;
	}

	cdev->fclk = devm_clk_get(&pdev->dev, "VPUCLK");
	if (IS_ERR(cdev->fclk)) {
		dev_err(&pdev->dev, "cannot get codaL func clock\n");
		ret = PTR_ERR(cdev->fclk);
		goto err_fclk;
	}

	cdev->aclk = devm_clk_get(&pdev->dev, "VPUACLK");
	if (IS_ERR(cdev->aclk)) {
		dev_err(&pdev->dev, "cannot get codaL axi clock\n");
		ret = PTR_ERR(cdev->aclk);
		goto err_aclk;
	}

	cdev->reg_base = (void *)ioremap(res->start, resource_size(res));
	if (!cdev->reg_base) {
		dev_err(&pdev->dev, "can't remap register area\n");
		ret = -ENOMEM;
		goto err_reg_base;
	}

	platform_set_drvdata(pdev, cdev);

	cdev->dev = &pdev->dev;
	cdev->uio_info.name = UIO_CODAL_NAME;
	cdev->uio_info.version = UIO_CODAL_VERSION;
	cdev->uio_info.mem[0].internal_addr = (void __iomem *)cdev->reg_base;
	cdev->uio_info.mem[0].addr = res->start;
	cdev->uio_info.mem[0].memtype = UIO_MEM_PHYS;
	cdev->uio_info.mem[0].size = resource_size(res);

#ifdef CONFIG_MMP_PM_DOMAIN_COMMON
	pm_runtime_enable(cdev->dev);
#endif

	/*
	 * this 2MB buffer contains firmware buffer, working buffer
	 * and parameter buffer, which shares in multi-process.
	 */
	mem_vir_addr = (void *)__get_free_pages(GFP_DMA | GFP_KERNEL,
					get_order(VDEC_WORKING_BUFFER_SIZE));
	if (!mem_vir_addr) {
		ret = -ENOMEM;
		goto err_uio_mem;
	}
	mem_dma_addr = (dma_addr_t)__virt_to_phys((unsigned long int)mem_vir_addr);

#ifdef CONFIG_ARM_SMMU
	if (pxa_ion_iommu_mapping)
		iommu_map(pxa_ion_iommu_mapping->domain, mem_dma_addr,
				mem_dma_addr, VDEC_WORKING_BUFFER_SIZE,
				IOMMU_READ | IOMMU_WRITE);
#endif

	cdev->uio_info.mem[1].internal_addr = (void __iomem *)mem_vir_addr;
	cdev->uio_info.mem[1].addr = mem_dma_addr;
	cdev->uio_info.mem[1].memtype = UIO_MEM_PHYS;
	cdev->uio_info.mem[1].size = VDEC_WORKING_BUFFER_SIZE;
	dev_info(&pdev->dev, "[1] internal addr[%p], addr[0x%08x] size[%ld]\n",
			cdev->uio_info.mem[1].internal_addr,
			(unsigned int)cdev->uio_info.mem[1].addr,
			cdev->uio_info.mem[1].size);

	if (sram_res) {
		cdev->uio_info.mem[2].addr = sram_res->start;
		cdev->uio_info.mem[2].memtype = UIO_MEM_PHYS;
		cdev->uio_info.mem[2].size = resource_size(sram_res);
		dev_info(&pdev->dev, "[2] addr[0x%08x] size[%ld]\n",
				(unsigned int)cdev->uio_info.mem[2].addr,
				cdev->uio_info.mem[2].size);
	}

	cdev->uio_info.irq_flags = IRQF_DISABLED;
	cdev->uio_info.irq = irq_func;
	cdev->uio_info.handler = codaL_irq_handler;
	cdev->uio_info.priv = cdev;

	cdev->uio_info.open = codaL_open;
	cdev->uio_info.release = codaL_release;
	cdev->uio_info.ioctl = codaL_ioctl;
	cdev->uio_info.mmap = NULL;

	if (IS_ENABLED(CONFIG_OF)) {
		struct device_node *np = pdev->dev.of_node;
		if (!np) {
			ret = -EINVAL;
			goto err_uio_mem;
		}
		if (of_property_read_u32(np, "marvell,codaL-type",
					&codaL_type)) {
			ret = -EINVAL;
			goto err_uio_mem;
		}

		if (of_property_read_u32(np, "marvell,sram-internal",
					&sram_internal)) {
			ret = -EINVAL;
			goto err_uio_mem;
		}
		if (of_property_read_u32(np, "marvell,nv21_support",
					&nv21_support)) {
			ret = -EINVAL;
			goto err_uio_mem;
		}

		if (of_property_read_u32(np, "marvell,seconddaxi_none",
					&seconddaxi_none)) {
			seconddaxi_none = SECONDAXI_SUPPORT;
		}
		cdev->coda_features =
			VPU_FEATURE_BITMASK_SRAM(sram_internal) |
			VPU_FEATURE_BITMASK_NV21(nv21_support) |
			VPU_FEATURE_BITMASK_NO2NDAXI(seconddaxi_none);
		cdev->codaL_type = codaL_type;
	} else {
		if (pdev->dev.platform_data)
			cdev->coda_features =
				*((unsigned int *)(pdev->dev.platform_data));
	}

	mutex_init(&(cdev->mutex));
	cdev->filehandle_ins_num = 0;
	for (i = 0; i < MAX_NUM_VPUSLOT; i++)
		fd_codecinst_arr[i].vpuslotidx = i;

	ret = uio_register_device(&pdev->dev, &cdev->uio_info);
	if (ret) {
		dev_err(&pdev->dev, "failed to register uio device\n");
		goto err_uio_register;
	}

	return 0;

err_uio_register:
	free_pages((unsigned long)cdev->uio_info.mem[1].internal_addr,
			get_order(VDEC_WORKING_BUFFER_SIZE));
err_uio_mem:
	iounmap(cdev->uio_info.mem[0].internal_addr);
err_reg_base:
	clk_put(cdev->aclk);
err_aclk:
	clk_put(cdev->fclk);
err_fclk:
	devm_kfree(&pdev->dev, cdev);

	return ret;
}

static int codaL_remove(struct platform_device *pdev)
{
	struct uio_codaL_dev *cdev = platform_get_drvdata(pdev);

	uio_unregister_device(&cdev->uio_info);
	free_pages((unsigned long)cdev->uio_info.mem[1].internal_addr,
			get_order(VDEC_WORKING_BUFFER_SIZE));
	iounmap(cdev->uio_info.mem[0].internal_addr);

	clk_put(cdev->aclk);
	clk_put(cdev->fclk);

#ifdef CONFIG_MMP_PM_DOMAIN_COMMON
	pm_runtime_disable(cdev->dev);
#endif

	return 0;
}

#ifndef CONFIG_MMP_PM_DOMAIN_COMMON
static bool is_suspend;
#endif

static int codaL_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct uio_codaL_dev *cdev = platform_get_drvdata(pdev);
	int i;
	bool cannot_off = false;

	if (cdev->filehandle_ins_num <= 0)
		return 0;
	if (cdev->clk_status != 0)
		return 0;

	for (i = 0; i < MAX_NUM_VPUSLOT; i++) {
		if ((fd_codecinst_arr[i].occupied == 1) &&
			((fd_codecinst_arr[i].codectypeid & VPU_CODEC_TYPEID_ENC_MASK)
			 || fd_codecinst_arr[i].codectypeid == 0)) {
			cannot_off = true;
			break;
		}
	}

#ifdef CONFIG_MMP_PM_DOMAIN_COMMON
	if (!cannot_off)
		return -EAGAIN;
#else
	if (!cannot_off) {
		dev_info(dev, "codaL poweroff in suspend\n");
		codaL_power_off(cdev);
		is_suspend = true;
	}
#endif

	return 0;
}

static int codaL_resume(struct device *dev)
{
#ifndef CONFIG_MMP_PM_DOMAIN_COMMON
	struct platform_device *pdev = to_platform_device(dev);
	struct uio_codaL_dev *cdev = platform_get_drvdata(pdev);

	if (is_suspend) {
		dev_info(dev, "codaL poweron in resume\n");
		codaL_power_on(cdev);
		is_suspend = false;
	}
#endif

	return 0;
}

static void codaL_shutdown(struct platform_device *dev)
{
}

static const struct dev_pm_ops codaL_pm_ops = {
	.suspend = codaL_suspend,
	.resume	= codaL_resume,
};

static struct of_device_id codaL_dt_ids[] = {
	{ .compatible = "mrvl,mmp-codaL",},
	{}
};

static struct platform_driver codaL_driver = {
	.probe = codaL_probe,
	.remove = codaL_remove,
	.shutdown = codaL_shutdown,
	.driver = {
		.name = UIO_CODAL_NAME,
		.owner = THIS_MODULE,
		.of_match_table = codaL_dt_ids,
		.pm	= &codaL_pm_ops,
	},
};

module_platform_driver(codaL_driver);

MODULE_AUTHOR("Leo Song (liangs@marvell.com)");
MODULE_DESCRIPTION("UIO driver for Chip and Media CodaL codec");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" UIO_CODAL_NAME);
