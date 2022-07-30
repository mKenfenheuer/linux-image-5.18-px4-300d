#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/font.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include "ums8485md.h"
#include "ilogo.h"

#define DRV_NAME "lcm"
#define DRV_VERSION "V0.4"
#define STATIC_LCM_MINOR 1
#define LCM_MINOR 128
#define LCM_FB_SIZE 1030

#define GPIO_BASE 963
#define LCD_SI (GPIO_BASE + 6)
#define LCD_SCL (GPIO_BASE + 7)
#define LCD_RS (GPIO_BASE + 16)
#define LCD_A0 (GPIO_BASE + 19)
#define LCD_CS1 (GPIO_BASE + 21)

enum lcm_a0_state
{
	LCM_CONTROL_DATA = 0,
	LCM_DISPLAY_DATA,
};

long lcm_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t read_fb_proc(struct file *filp, char *buf, size_t count, loff_t *offp);
static ssize_t write_fb_proc(struct file *filp, const char *buf, size_t count, loff_t *offp);
static ssize_t read_reset_proc(struct file *filp, char *buf, size_t count, loff_t *offp);
static ssize_t write_reset_proc(struct file *filp, const char *buf, size_t count, loff_t *offp);
static ssize_t read_update_proc(struct file *filp, char *buf, size_t count, loff_t *offp);
static ssize_t write_update_proc(struct file *filp, const char *buf, size_t count, loff_t *offp);
static size_t read_index = 0;
static size_t write_index = 0;

struct proc_ops proc_fb_fops = {
	proc_read : read_fb_proc,
	proc_write : write_fb_proc
};

struct proc_ops proc_reset_fops = {
	proc_read : read_reset_proc,
	proc_write : write_reset_proc
};

struct proc_ops proc_update_fops = {
	proc_read : read_update_proc,
	proc_write : write_update_proc
};

struct proc_dir_entry *proc_dir;

static int lcm_initdata[] = {0xa0, 0x2f, 0xa2, 0xc8, 0x27, 0x81, 0x17, 0xaf};
// static int lcm_standby[] = { 0xad, 0x02, 0xaf, 0xa5, 0xa4};
static char fbuffer[LCM_FB_SIZE] = {0x00};

static struct proc_dir_entry *proc_entry;

void lcm_delay(unsigned char delay)
{
	unsigned timer;
	while (delay)
	{
		timer = 0x300;
		while (timer)
			timer--;
		delay--;
	}
}

void scl_hi(void)
{
	lcm_delay(1);
	gpio_set_value(LCD_SCL, 1);
	lcm_delay(1);
}

void scl_lo(void)
{
	lcm_delay(1);
	gpio_set_value(LCD_SCL, 0);
	lcm_delay(1);
}

void si_hi(void)
{
	lcm_delay(1);
	gpio_set_value(LCD_SI, 1);
	lcm_delay(1);
}

void si_lo(void)
{
	lcm_delay(1);
	gpio_set_value(LCD_SI, 0);
	lcm_delay(1);
}

void cs_hi(void)
{
	lcm_delay(1);
	gpio_set_value(LCD_CS1, 1);
	lcm_delay(1);
}

void cs_lo(void)
{
	lcm_delay(1);
	gpio_set_value(LCD_CS1, 0);
	lcm_delay(1);
}

int lcm_open(struct inode *inode, struct file *file)
{

	return 0;
}

void write_lcm_data(int type, unsigned char data)
{
	int i;
	cs_lo();
	if (type)
	{ // display data
		lcm_delay(1);
		gpio_set_value(LCD_A0, LCM_DISPLAY_DATA); /* display data */
		lcm_delay(1);
	}
	else
	{ // control data
		lcm_delay(1);
		gpio_set_value(LCD_A0, LCM_CONTROL_DATA); /* control data */
		lcm_delay(1);
	}

	for (i = 0; i < 8; i++)
	{
		scl_lo();
		if (data & 0x80)
			si_hi();
		else
			si_lo();
		scl_hi();
		lcm_delay(1);
		data <<= 1;
	}
	cs_hi();
}

void display_fbuffer(void)
{
	int i, j, col;

	col = 0;
	for (i = 0; i < 8; i++)
	{
		write_lcm_data(LCM_CONTROL_DATA, (0x0f & col));
		write_lcm_data(LCM_CONTROL_DATA, (0x10 | (0x0f & col >> 4)));
		write_lcm_data(LCM_CONTROL_DATA, (0xb0 + i));

		for (j = 0; j < 128; j++)
		{
			write_lcm_data(LCM_DISPLAY_DATA, fbuffer[j + 128 * i]);
		}
	}
}

void display_ilogo(void)
{
	memcpy(&fbuffer, &ilogo, 1030);
	display_fbuffer();
}

void display_reset(void)
{
	memset(fbuffer, 0, sizeof fbuffer);
	display_fbuffer();
}

int write_lcm(lcm_member_t buf)
{
	int i;
	write_lcm_data(LCM_CONTROL_DATA, (0xb0 + buf.page));
	write_lcm_data(LCM_CONTROL_DATA, (0x0f & buf.column));
	write_lcm_data(LCM_CONTROL_DATA, (0x10 | (0x0f & buf.column >> 4)));
	if (buf.ctrl != WIX_LCM_CMD_RESET)
	{
		for (i = 0; i < buf.size; i++)
			write_lcm_data(LCM_DISPLAY_DATA, buf.data[i]);
	}

	switch (buf.ctrl)
	{
	case WIX_LCM_CMD_PON:
		write_lcm_data(LCM_CONTROL_DATA, 0xaf);
		break;
	case WIX_LCM_CMD_POFF:
		write_lcm_data(LCM_CONTROL_DATA, 0xae);
		break;
	case WIX_LCM_CMD_RESET:
		for (i = 0; i < ARRAY_SIZE(lcm_initdata); i++)
		{
			write_lcm_data(LCM_CONTROL_DATA, lcm_initdata[i]);
		}
		display_ilogo();
		break;
	case WIX_LCM_CMD_DISP_NORMAL:
		write_lcm_data(LCM_CONTROL_DATA, 0xa6);
		break;
	case WIX_LCM_CMD_DISP_REVERSE:
		write_lcm_data(LCM_CONTROL_DATA, 0xa7);
		break;
	case WIX_LCM_CMD_ENTIRE_DISP_ON:
		write_lcm_data(LCM_CONTROL_DATA, 0xa5);
		break;
	case WIX_LCM_CMD_ENTIRE_DISP_OFF:
		write_lcm_data(LCM_CONTROL_DATA, 0xa4);
		break;
	case WIX_LCM_CMD_ADC_SELECT_NORMAL:
		write_lcm_data(LCM_CONTROL_DATA, 0xa0);
		break;
	case WIX_LCM_CMD_ADC_SELECT_REVERSE:
		write_lcm_data(LCM_CONTROL_DATA, 0xa1);
		break;
	case WIX_LCM_CMD_OUTPUT_NORMAL:
		write_lcm_data(LCM_CONTROL_DATA, 0xc0);
		break;
	case WIX_LCM_CMD_OUTPUT_REVERSE:
		write_lcm_data(LCM_CONTROL_DATA, 0xc8);
		break;
	case WIX_LCM_CMD_WRITE_DATA:
		break;
	default:
		pr_info("[%s] Unknow ctrl command. \n", __func__);
		break;
	}
	return 0;
}

int data_check(lcm_member_t buf)
{
	if ((buf.page < 0) || (buf.page > 7))
	{
		return -1;
	}
	if ((buf.column < 0) || (buf.column > 127))
		return -1;
	return 0;
}

long lcm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	lcm_member_t info;

	if (copy_from_user(&info, (lcm_member_t *)arg, sizeof(lcm_member_t)))
	{
		pr_err("[%s]->[%s] can't opy data from the user space!\n", DRV_NAME, __func__);
		return -EFAULT;
	}

	if (data_check(info))
		return -EFAULT;

	switch (cmd)
	{
	case IOCTL_DISPLAY_COMMAND:
		write_lcm(info);
		break;
	default:
		pr_info("[%s]->[%s] Unknown IOCTL command\n", DRV_NAME, __func__);
		break;
	};

	return 0;
}

int lcm_close(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations lcm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = lcm_ioctl,
	.compat_ioctl = lcm_ioctl,
	.open = lcm_open,
	.release = lcm_close};

static struct miscdevice lcm_dev = {

#if STATIC_LCM_MINOR
	LCM_MINOR,
#else
	MISC_DYNAMIC_MINOR,
#endif
	"lcm",
	&lcm_fops,
};

static int lcm_gpio_init(void)
{
	int i, ret;
	char name[5];
	u32 all_lcm_pin[5] = {LCD_SI, LCD_SCL, LCD_RS, LCD_A0, LCD_CS1};
	for (i = 0; i < 5; i++)
	{
		sprintf(name, "lcm%d", i);
		// register, turn off
		ret = gpio_request_one(all_lcm_pin[i], GPIOF_OUT_INIT_LOW, name);

		if (ret)
		{
			// printk(KERN_ERR "/dev/lcm Unable to request GPIO %d: %d\n", all_lcm_pin[i], ret);
			// return ret;
		}
	}

	/* Initial LCM GPIO pin */
	gpio_set_value(LCD_SI, 0);
	gpio_set_value(LCD_SCL, 0);
	gpio_set_value(LCD_A0, 0);
	gpio_set_value(LCD_RS, 0);
	gpio_set_value(LCD_CS1, 0);
	mdelay(200);
	gpio_set_value(LCD_RS, 1);
	mdelay(200);
	gpio_set_value(LCD_CS1, 1);
	gpio_set_value(LCD_SCL, 1);

	for (i = 0; i < ARRAY_SIZE(lcm_initdata); i++)
	{
		write_lcm_data(LCM_CONTROL_DATA, lcm_initdata[i]);
	}
	display_fbuffer();
	return 0;
}

static ssize_t read_fb_proc(struct file *filp, char *buf, size_t len, loff_t *offp)
{
	if (read_index == LCM_FB_SIZE)
	{
		read_index = 0;
		return 0;
	}

	if (read_index + len > LCM_FB_SIZE)
	{
		len = LCM_FB_SIZE - read_index;
	}

	if (copy_to_user(buf, &fbuffer, len))
	{
		return -2;
	}

	read_index += len;

	return len;
}

static ssize_t write_fb_proc(struct file *filp, const char *buff, size_t len, loff_t *offp)
{

	if (write_index == LCM_FB_SIZE)
	{
		write_index = 0;
	}

	if (write_index + len > LCM_FB_SIZE)
	{
		len = LCM_FB_SIZE - write_index;
	}

	if (copy_from_user(&fbuffer[write_index], buff, len))
	{
		return -2;
	}

	write_index += len;

	return len;
}

static ssize_t read_reset_proc(struct file *filp, char *buf, size_t len, loff_t *offp)
{
	return -1;
}

static ssize_t write_reset_proc(struct file *filp, const char *buff, size_t len, loff_t *offp)
{
	write_index = 0;
	display_reset();
	return len;
}

static ssize_t read_update_proc(struct file *filp, char *buf, size_t len, loff_t *offp)
{
	return -1;
}

static ssize_t write_update_proc(struct file *filp, const char *buff, size_t len, loff_t *offp)
{
	write_index = 0;
	display_fbuffer();
	return len;
}

static int __init lcm_init(void)
{
	int ret = 0;
	ret = misc_register(&lcm_dev);
	if (ret)
	{
		pr_err("could not register the lcm driver.\n");
		return ret;
	}
	lcm_gpio_init();
	pr_info("lcm driver is registered.\n");

	display_ilogo();

	proc_dir = proc_mkdir("ums8485md", NULL);
	proc_entry = proc_create("fb", 0666, proc_dir, &proc_fb_fops);
	proc_entry = proc_create("reset", 0666, proc_dir, &proc_reset_fops);
	proc_entry = proc_create("display", 0666, proc_dir, &proc_update_fops);

	return ret;
}
static void __exit lcm_exit(void)
{
	int i;
	u32 all_lcm_pin[5] = {LCD_SI, LCD_SCL, LCD_RS, LCD_A0, LCD_CS1};

	misc_deregister(&lcm_dev);
	for (i = 0; i < 5; i++)
	{
		// deregister
		gpio_free(all_lcm_pin[i]);
	}

	remove_proc_entry("fb", proc_dir);
	remove_proc_entry("display", proc_dir);
	remove_proc_entry("reset", proc_dir);
	remove_proc_entry("ums8485md", NULL);
}

MODULE_DESCRIPTION("LCM Driver");
MODULE_LICENSE("GPL");

MODULE_SOFTDEP("pre: gpio-ich");

module_init(lcm_init);
module_exit(lcm_exit);
