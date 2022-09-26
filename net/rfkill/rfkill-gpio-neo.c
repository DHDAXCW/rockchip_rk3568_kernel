// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022, Kyosuke Nekoyashiki <supercatexpert@gmail.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/kthread.h>

struct rfkill_gpio_neo_data {
	const char		*name;
	enum rfkill_type	type;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*block_gpio;

	u32			power_on_wait_time;
	u32			reset_active_time;
	u32			reset_wait_time;

	struct rfkill		*rfkill_dev;
};

static int rfkill_gpio_neo_set_block(void *data, bool blocked)
{
	struct rfkill_gpio_neo_data *rfkill = data;

	gpiod_set_value_cansleep(rfkill->block_gpio, blocked);

	return 0;
}

static const struct rfkill_ops rfkill_gpio_neo_ops = {
	.set_block = rfkill_gpio_neo_set_block,
};


static int rfkill_gpio_neo_do_reset(void *p) {
	struct rfkill_gpio_neo_data *rfkill = (struct rfkill_gpio_neo_data *)p;

	if (rfkill->power_on_wait_time > 10) {
		mdelay(rfkill->power_on_wait_time);
	} else {
		mdelay(10);
	}

	gpiod_set_value_cansleep(rfkill->reset_gpio, 1);
	mdelay(rfkill->reset_active_time);
	gpiod_set_value_cansleep(rfkill->reset_gpio, 0);

	if (rfkill->reset_wait_time > 10) {
		mdelay(rfkill->reset_wait_time);
	} else {
		mdelay(10);
	}

	return 0;
}


static int rfkill_gpio_neo_probe(struct platform_device *pdev)
{
	struct rfkill_gpio_neo_data *rfkill;
	struct gpio_desc *gpio;
	const char *type_name;
	int ret;

	rfkill = devm_kzalloc(&pdev->dev, sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	device_property_read_string(&pdev->dev, "name", &rfkill->name);
	device_property_read_string(&pdev->dev, "type", &type_name);
	device_property_read_u32(&pdev->dev, "power-on-wait-time", &rfkill->power_on_wait_time);
	device_property_read_u32(&pdev->dev, "reset-active-time", &rfkill->reset_active_time);
	device_property_read_u32(&pdev->dev, "reset-wait-time", &rfkill->reset_wait_time);

	if (!rfkill->name)
		rfkill->name = dev_name(&pdev->dev);

	rfkill->type = rfkill_find_type(type_name);

	if (rfkill->power_on_wait_time > 30000) {
		rfkill->power_on_wait_time = 0;
	}

	if (rfkill->reset_active_time < 10 || rfkill->reset_active_time > 1000) {
		rfkill->reset_active_time = 10;
	}

	if (rfkill->reset_wait_time > 30000) {
		rfkill->reset_wait_time = 0;
	}

	gpio = devm_gpiod_get(&pdev->dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	rfkill->power_gpio = gpio;

	gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	rfkill->reset_gpio = gpio;

	gpio = devm_gpiod_get(&pdev->dev, "block", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	rfkill->block_gpio = gpio;

	/* Make sure at-least one GPIO is defined for this instance */
	if (!rfkill->block_gpio) {
		dev_err(&pdev->dev, "invalid platform data\n");
		return -EINVAL;
	}

	rfkill->rfkill_dev = rfkill_alloc(rfkill->name, &pdev->dev,
					  rfkill->type, &rfkill_gpio_neo_ops,
					  rfkill);
	if (!rfkill->rfkill_dev)
		return -ENOMEM;

	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto err_destroy;

	platform_set_drvdata(pdev, rfkill);

	dev_info(&pdev->dev, "%s device registered.\n", rfkill->name);

	if (rfkill->power_gpio) {
		gpiod_set_value_cansleep(rfkill->power_gpio, 1);
	}
	gpiod_set_value_cansleep(rfkill->block_gpio, 0);

	if (rfkill->reset_gpio) {
		gpiod_set_value_cansleep(rfkill->reset_gpio, 0);

		rfkill_gpio_neo_do_reset(rfkill);
	}

	return 0;

err_destroy:
	rfkill_destroy(rfkill->rfkill_dev);

	return ret;
}

static int rfkill_gpio_neo_remove(struct platform_device *pdev)
{
	struct rfkill_gpio_neo_data *rfkill = platform_get_drvdata(pdev);

	gpiod_set_value_cansleep(rfkill->block_gpio, 1);

	if(rfkill->power_gpio) {
		gpiod_set_value_cansleep(rfkill->power_gpio, 0);
	}

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);

	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id rfkill_gpio_neo_of_match[] = {
	{ .compatible = "rfkill-gpio-neo" },
	{}
};
MODULE_DEVICE_TABLE(of, wlan_platdata_of_match);
#endif /* CONFIG_OF */

static struct platform_driver rfkill_gpio_neo_driver = {
	.probe = rfkill_gpio_neo_probe,
	.remove = rfkill_gpio_neo_remove,
	.driver = {
		.name = "rfkill-gpio-neo",
		.owner = THIS_MODULE,
	        .of_match_table = of_match_ptr(rfkill_gpio_neo_of_match),
	},
};

module_platform_driver(rfkill_gpio_neo_driver);

MODULE_DESCRIPTION("Neo GPIO rfkill driver");
MODULE_AUTHOR("Kyosuke Nekoyashiki <supercatexpert@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rfkill-gpio-neo");
