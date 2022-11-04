// SPDX-License-Identifier: GPL-2.0
/**
 * switch-simple.c - typec switch simple control.
 *
 * Copyright 2020 NXP
 * Author: Jun Li <jun.li@nxp.com>
 *
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/usb/typec_mux.h>

struct typec_switch_simple {
	struct typec_switch *sw;
	struct mutex lock;
	struct gpio_desc *sel_gpio;
};

static int typec_switch_simple_set(struct typec_switch *sw,
				   enum typec_orientation orientation)
{
	struct typec_switch_simple *typec_sw = typec_switch_get_drvdata(sw);

	mutex_lock(&typec_sw->lock);

	switch (orientation) {
	case TYPEC_ORIENTATION_NORMAL:
		if (typec_sw->sel_gpio)
			gpiod_set_value_cansleep(typec_sw->sel_gpio, 1);
		break;
	case TYPEC_ORIENTATION_REVERSE:
		if (typec_sw->sel_gpio)
			gpiod_set_value_cansleep(typec_sw->sel_gpio, 0);
		break;
	case TYPEC_ORIENTATION_NONE:
		break;
	}

	mutex_unlock(&typec_sw->lock);

	return 0;
}

static int typec_switch_simple_probe(struct platform_device *pdev)
{
	struct typec_switch_simple	*typec_sw;
	struct device			*dev = &pdev->dev;
	struct typec_switch_desc sw_desc;

	typec_sw = devm_kzalloc(dev, sizeof(*typec_sw), GFP_KERNEL);
	if (!typec_sw)
		return -ENOMEM;

	platform_set_drvdata(pdev, typec_sw);

	sw_desc.drvdata = typec_sw;
	sw_desc.fwnode = dev->fwnode;
	sw_desc.set = typec_switch_simple_set;
	mutex_init(&typec_sw->lock);

	/* Get the super speed active channel selection GPIO */
	typec_sw->sel_gpio = devm_gpiod_get_optional(dev, "switch",
						     GPIOD_OUT_LOW);
	if (IS_ERR(typec_sw->sel_gpio))
		return PTR_ERR(typec_sw->sel_gpio);

	typec_sw->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(typec_sw->sw)) {
		dev_err(dev, "Error registering typec switch: %ld\n",
			PTR_ERR(typec_sw->sw));
		return PTR_ERR(typec_sw->sw);
	}

	return 0;
}

static int typec_switch_simple_remove(struct platform_device *pdev)
{
	struct typec_switch_simple *typec_sw = platform_get_drvdata(pdev);

	typec_switch_unregister(typec_sw->sw);

	return 0;
}

static const struct of_device_id of_typec_switch_simple_match[] = {
	{ .compatible = "typec-orientation-switch" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_typec_switch_simple_match);

static struct platform_driver typec_switch_simple_driver = {
	.probe		= typec_switch_simple_probe,
	.remove		= typec_switch_simple_remove,
	.driver		= {
		.name	= "typec-switch-simple",
		.of_match_table = of_typec_switch_simple_match,
	},
};

module_platform_driver(typec_switch_simple_driver);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TypeC Orientation Switch Simple driver");
MODULE_AUTHOR("Jun Li <jun.li@nxp.com>");
