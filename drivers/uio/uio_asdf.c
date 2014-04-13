#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <linux/module.h>
static struct uio_info myfpga_uio_info = {
   .name = "uio_myfpga",
   .version = "0.1",
   .irq=72,
   .irq_flags=IRQF_TRIGGER_RISING,
};
static struct platform_device *myfpga_uio_pdev;

static int __init myfpga_init(void)
{
    myfpga_uio_pdev = platform_device_register_resndata (NULL,
                                                         "uio_pdrv_genirq",
                                                         -1,
                                                         NULL,
                                                         0,
                                                         &myfpga_uio_info,
                                                         sizeof(struct uio_info)
                                                        );
    if (IS_ERR(myfpga_uio_pdev)) {
        return PTR_ERR(myfpga_uio_pdev);
    }
    return 0;
}
static void __exit myfpga_exit(void)
{
    platform_device_unregister(myfpga_uio_pdev);
}
module_init(myfpga_init);
module_exit(myfpga_exit);
