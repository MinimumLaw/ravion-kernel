#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>

#include <asm/uaccess.h>
#include <mach/gpio.h>

#define BABBAGE_SW12V_EN		(0*32 + 1)	/* GPIO_1_1 */
#define BABBAGE_WDOG_B			(0*32 + 4)	/* GPIO_1_4 */
#define BABBAGE_SD2_WP			(0*32 + 5)	/* GPIO_1_5 */
#define BABBAGE_SD2_CD			(0*32 + 6)	/* GPIO_1_6 */
#define BABBAGE_USBH1_HUB_RST_N		(0*32 + 7)	/* GPIO_1_7 */

#define BABBAGE_GPO_3			(1*32 + 1)	/* GPIO_2_1 */
#define BABBAGE_GPI_0			(1*32 + 2)	/* GPIO_2_2 */
#define BABBAGE_GPI_1			(1*32 + 4)	/* GPIO_2_4 */
#define BABBAGE_USB_PHY_RESET_B		(1*32 + 5)	/* GPIO_2_5 */
#define BABBAGE_GPI_2			(1*32 + 6)	/* GPIO_2_6 */
#define BABBAGE_GPI_3			(1*32 + 7)	/* GPIO_2_7 */
#define BABBAGE_GPO_0			(1*32 + 12)	/* GPIO_2_12 */
#define BABBAGE_GPO_1			(1*32 + 13)	/* GPIO_2_13 */
#define BABBAGE_FEC_PHY_RESET		(1*32 + 14)	/* GPIO_2_14 */
#define BABBAGE_BT_ENABLE		(1*32 + 15)	/* GPIO_2_15 */
#define BABBAGE_WL_ENABLE		(1*32 + 16)	/* GPIO_2_16 */
#define BABBAGE_GPO_2			(1*32 + 17)	/* GPIO_2_17 */
#define BABBAGE_CAN_RESET_B		(1*32 + 18)	/* GPIO_2_18 */
#define BABBAGE_POWER_KEY		(1*32 + 21)	/* GPIO_2_21 */
#define BABBAGE_EIM_RESET		(1*32 + 26)	/* GPIO_2_26 */

#define BABBAGE_26M_OSC_EN		(2*32 + 1)	/* GPIO_3_1 */
#define BABBAGE_LVDS_POWER_DOWN_B	(2*32 + 3)	/* GPIO_3_3 */
#define BABBAGE_VDAC_POWER_DOWN_B	(2*32 + 8)	/* GPIO_3_8 */

#define BABBAGE_VADC_RESET_B		(3*32 + 11)	/* GPIO_4_11 */
#define BABBAGE_VADC_POWER_DOWN_B	(3*32 + 12)	/* GPIO_4_12 */
#define BABBAGE_USB_CLK_EN_B		(3*32 + 13)	/* GPIO_4_13 */
#define BABBAGE_3V3_ON			(3*32 + 15)	/* GPIO_4_15 */
#define BABBAGE_AUDIO_CLK_EN_B		(3*32 + 26)	/* GPIO_4_26 */

#define GPIO_TOTAL	28

static struct proc_dir_entry *proc_gpio_parent;
static struct proc_dir_entry *proc_gpios[GPIO_TOTAL];

typedef struct
{
	int	gpio;
	char    name[32];
} gpio_summary_type;

static gpio_summary_type gpio_summaries[GPIO_TOTAL] =
{
	{ BABBAGE_SW12V_EN,		"sw12v-en" },
	{ BABBAGE_WDOG_B,		"pwr-off_n" },
	{ BABBAGE_SD2_WP,		"sd2-wp" },
	{ BABBAGE_SD2_CD,		"sd2-cd" },
	{ BABBAGE_USBH1_HUB_RST_N,	"usb-hub-rst_n" },
	{ BABBAGE_GPO_3,		"gpo-3" },
	{ BABBAGE_GPI_0,		"gpi-0" },
	{ BABBAGE_GPI_1,		"gpi-1" },
	{ BABBAGE_USB_PHY_RESET_B,	"usb-phy-reset_n" },
	{ BABBAGE_GPI_2,		"gpi-2" },
	{ BABBAGE_GPI_3,		"gpi-3" },
	{ BABBAGE_GPO_0,		"gpo-0" },
	{ BABBAGE_GPO_1,		"gpo-1" },
	{ BABBAGE_FEC_PHY_RESET,	"fec-phy-reset_n" },
	{ BABBAGE_BT_ENABLE,		"bt-en" },
	{ BABBAGE_WL_ENABLE,		"wl-en" },
	{ BABBAGE_GPO_2,		"gpo-2" },
	{ BABBAGE_CAN_RESET_B,		"can-rst_n" },
	{ BABBAGE_POWER_KEY, 		"pwr-key" },
	{ BABBAGE_EIM_RESET, 		"eim-reset" },
	{ BABBAGE_26M_OSC_EN, 		"26m-osc-en" },
	{ BABBAGE_LVDS_POWER_DOWN_B, 	"lvds-power-down_n" },
	{ BABBAGE_VDAC_POWER_DOWN_B, 	"vdac-power-down_n" },
	{ BABBAGE_VADC_RESET_B, 	"vadc-reset_n" },
	{ BABBAGE_VADC_POWER_DOWN_B, 	"vadc-power-down_n" },
	{ BABBAGE_USB_CLK_EN_B, 	"usb-clk-en_n" },
	{ BABBAGE_3V3_ON, 		"3v3_on" },
	{ BABBAGE_AUDIO_CLK_EN_B, 	"audio-clk-en_n" },
};

static int proc_gpio_write(struct file *file, const char __user *buf,
                           unsigned long count, void *data)
{
	char lbuf[count + 1];
	gpio_summary_type *summary = data;
	u32 altfn, direction, setclear, gafr;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	memset(lbuf, 0, count + 1);

	if (copy_from_user(lbuf, buf, count))
		return -EFAULT;

	if(lbuf[0] == '0')
		gpio_set_value(summary->gpio, 0);
	else
	if(lbuf[0] == '1')
		gpio_set_value(summary->gpio, 1);
	else
		return -EINVAL;

	return count;
}

static int proc_gpio_read(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	gpio_summary_type *summary = data;
	int len, value = -1;
	
	if(off > 0)
	{
		*eof = 1;
		return 0;
	}
	
	value = gpio_get_value(summary->gpio);
	len = sprintf(page, "%d\n", value);
	*start = page;
	return len;
}



static int __init gpio_init(void)
{
	int i;

	proc_gpio_parent = create_proc_entry("gpio", S_IFDIR | S_IRUGO | S_IXUGO, NULL);
	if(!proc_gpio_parent) return 0;

	for(i=0; i < GPIO_TOTAL; i++)
	{
		proc_gpios[i] = create_proc_entry(gpio_summaries[i].name, 0644, proc_gpio_parent);
		if(proc_gpios[i])
		{
			proc_gpios[i]->data = &gpio_summaries[i];
			proc_gpios[i]->read_proc = proc_gpio_read;
			proc_gpios[i]->write_proc = proc_gpio_write;
		}
	}

	return 0;
}

static void gpio_exit(void)
{
	int i;

	for(i=0; i < GPIO_TOTAL; i++)
	{
		if(proc_gpios[i]) remove_proc_entry(gpio_summaries[i].name, proc_gpio_parent);
	}
	if(proc_gpio_parent) remove_proc_entry("gpio", NULL);
}

module_init(gpio_init);
module_exit(gpio_exit);
MODULE_LICENSE("GPL");
