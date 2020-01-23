#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <ihk/ihk_host_driver.h>

#define DEV_CLASS_NAME "dev_class"
#define DEVICE_NAME "test_driver"

static int major_num = 0;
static struct class *test_class = NULL;
static struct device *test_dev = NULL;

struct test_driver_ioctl_arg {
	unsigned long addr;
	unsigned long val;
	unsigned long addr_ext;
	int cpu;
	int fake_os;
	int fake_cpu;
};

static int dev_open(struct inode *inode, struct file *file)
{

	return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* request:
	0: ihk_os_read_cpu_register
	1: ihk_os_write_cpu_register
*/
static long dev_ioctl(struct file *file, unsigned int request, unsigned long _arg)
{
	int ret;
	struct test_driver_ioctl_arg *__user uarg =
		(struct test_driver_ioctl_arg *__user)_arg;
	struct test_driver_ioctl_arg arg;
	struct ihk_os_cpu_register desc;
	ihk_os_t os;
	struct timespec start, stop;
	int old_cpu;

	if (copy_from_user(&arg, uarg, sizeof(struct test_driver_ioctl_arg))) {
		ret = -EFAULT;
		goto out;
	}

	pr_info("%s: request: %d, addr: %lx, val: %lx, addr_ext: %lx, "
		"cpu: %d, fake_os: %d, fake_cpu: %d\n",
		__func__, request, arg.addr, arg.val, arg.addr_ext,
		arg.cpu, arg.fake_os, arg.fake_cpu);
	desc.addr = arg.addr;
	desc.val = arg.val;
	desc.addr_ext = arg.addr_ext;
	atomic_set(&desc.sync, 0);
	old_cpu = arg.cpu;

	ret = ihk_get_request_os_cpu(&os, &arg.cpu);
	if (ret) {
		pr_err("%s:%d: error: "
		       "ihk_get_request_os_cpu returned%d\n",
		       __FILE__, __LINE__, ret);
		goto out;
	}

	pr_info("%s: original os: %lx, cpu: %d\n",
		__func__, (unsigned long)os, arg.cpu);

	if (arg.fake_os) {
		switch (arg.fake_os) {
		case 1:
			os = NULL;
			break;
		case 2: /* next OS */
			os = os + sizeof(void *);
			break;
		}
	}

	if (arg.fake_cpu) {
		arg.cpu = old_cpu;
	}

	pr_info("%s: disguised os: %lx, cpu: %d\n",
		__func__, (unsigned long)os, arg.cpu);

	switch (request) {
	case 0:
		ret = ihk_os_read_cpu_register(os, arg.cpu,
					       &desc);

		if (ret) {
			pr_err("%s: error: "
			       "ihk_os_read_cpu_register returned %d\n",
			       __func__, ret);
			goto out;
		}
		break;
	case 1:
		ret = ihk_os_write_cpu_register(os, arg.cpu,
						&desc);

		if (ret) {
			pr_err("%s: error: "
			       "ihk_os_write_cpu_register returned %d\n",
			       __func__, ret);
			goto out;
		}
		break;
	default:
		pr_err("%s:%d: error: unknown request: %d\n",
		       __FILE__, __LINE__, request);
		ret = -EINVAL;
		goto out;
	}

	/* wait until notified with 1 sec timeout */
	getnstimeofday(&start);
	while (!atomic_read(&desc.sync)) {
		mdelay(200);
		getnstimeofday(&stop);
		if (stop.tv_sec >= start.tv_sec + 1 &&
		    stop.tv_nsec >= start.tv_nsec) {
			break;
		}
		pr_info("%s:%d: waiting for notification...\n",
		       __FILE__, __LINE__);
	}

	arg.val = desc.val;
	if (copy_to_user(uarg, &arg, sizeof(struct test_driver_ioctl_arg))) {
		ret = -EFAULT;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

static struct file_operations fops = {
	.open = dev_open,
	.release = dev_release,
	.unlocked_ioctl = dev_ioctl,
};

static int register_device(void)
{
	major_num = register_chrdev(0, DEVICE_NAME, &fops);
	if (major_num < 0) {
		printk(KERN_ALERT "failed\n");
		return major_num;
	}

	test_class = class_create(THIS_MODULE, DEV_CLASS_NAME);

	test_dev = device_create(test_class, NULL, MKDEV(major_num, 0), NULL, DEVICE_NAME);

	return 0;
}

void unregister_device(void)
{
	device_destroy(test_class, MKDEV(major_num, 0));
	class_unregister(test_class);
	class_destroy(test_class);
	unregister_chrdev(major_num, DEVICE_NAME);
}

static int __init dev_init(void)
{
	register_device();
	return 0;
}

module_init(dev_init);

static void __exit dev_exit(void)
{
	unregister_device();
}

module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test for getrusage");
MODULE_VERSION("1.0");
