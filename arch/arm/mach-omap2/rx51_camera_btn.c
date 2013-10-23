/**
 * arch/arm/mach-omap2/rx51_camera_btn.c
 *
 * Driver for sending camera button events to input-layer
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Written by Henrik Saari <henrik.saari@nokia.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/camera_button.h>

#define CAM_IRQ_FLAGS (IRQF_TRIGGER_RISING | \
		       IRQF_TRIGGER_FALLING)

struct rx51_cam_button {
	struct input_dev	*input;

	int			focus;
	int			shutter;
};

static irqreturn_t rx51_cam_shutter_irq(int irq, void *_button)
{
	struct rx51_cam_button *button = _button;
	int gpio;

	gpio = irq_to_gpio(irq);
	input_report_key(button->input, KEY_CAMERA,
			!gpio_get_value(gpio));
	input_sync(button->input);

	return IRQ_HANDLED;
}

static irqreturn_t rx51_cam_focus_irq(int irq, void *_button)
{
	struct rx51_cam_button *button = _button;
	int gpio;

	gpio = irq_to_gpio(irq);
	input_report_key(button->input, KEY_F10,
			!gpio_get_value(gpio));
	input_sync(button->input);

	return IRQ_HANDLED;
}

/* SYSFS */
static ssize_t rx51_show_focus(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct rx51_cam_button *button = dev_get_drvdata(dev);

	/* report 1 when button is pressed. */
	return sprintf(buf, "%d\n", !gpio_get_value(button->focus));
}

static DEVICE_ATTR(focus_btn, S_IRUGO, rx51_show_focus, NULL);

static ssize_t rx51_show_shutter(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct rx51_cam_button *button = dev_get_drvdata(dev);

	/* report 1 when button is pressed. */
	return sprintf(buf, "%d\n", !gpio_get_value(button->shutter));
}

static DEVICE_ATTR(shutter_btn, S_IRUGO, rx51_show_shutter, NULL);

static int __init rx51_camera_btn_probe(struct platform_device *pdev)
{
	struct camera_button_platform_data *pdata = pdev->dev.platform_data;
	struct rx51_cam_button *button;
	int err = 0;

	button = kzalloc(sizeof(*button), GFP_KERNEL);
	if (!button) {
		err = -ENOMEM;
		goto err_alloc;
	}

	if (!pdata || !pdata->shutter) {
		dev_err(&pdev->dev, "Missing platform_data\n");
		err = -EINVAL;
		goto err_pdata;
	}

	err = gpio_request(pdata->shutter, "shutter");
	if (err) {
		dev_err(&pdev->dev, "Cannot request gpio %d\n", pdata->shutter);
		goto err_shutter;
	}
	gpio_direction_input(pdata->shutter);

	err = request_irq(gpio_to_irq(pdata->shutter), rx51_cam_shutter_irq,
			  CAM_IRQ_FLAGS, "cam_shuter_btn", button);
	if (err) {
		dev_err(&pdev->dev, "Could not request irq %d\n",
				gpio_to_irq(pdata->shutter));
		goto err_irq_shutter;
	}
	button->shutter = pdata->shutter;

	if (pdata->focus) {
		err = gpio_request(pdata->focus, "focus");
		if (err) {
			dev_err(&pdev->dev, "Cannot request gpio %d\n",
					pdata->focus);
			goto err_focus;
		}

		gpio_direction_input(pdata->focus);

		err = request_irq(gpio_to_irq(pdata->focus),
				rx51_cam_focus_irq, CAM_IRQ_FLAGS,
				"cam_focus_btn", button);
		if (err) {
			dev_err(&pdev->dev, "Could not request irq\n");
			goto err_irq_focus;
		}
		button->focus = pdata->focus;
	}

	dev_set_drvdata(&pdev->dev, button);

	button->input = input_allocate_device();
	if (!button->input) {
		dev_err(&pdev->dev, "Unable to allocate input device\n");
		err = -ENOMEM;
		goto err_input_alloc;
	}

	button->input->evbit[0] = BIT_MASK(EV_KEY);
	button->input->keybit[BIT_WORD(KEY_CAMERA)] =
		BIT_MASK(KEY_CAMERA);
	button->input->name = "camera button";

	if (pdata->focus)
		button->input->keybit[BIT_WORD(KEY_F10)] =
			BIT_MASK(KEY_F10);

	err = input_register_device(button->input);
	if (err)
		goto err_input_reg;

	if (device_create_file(&pdev->dev, &dev_attr_focus_btn))
		dev_err(&pdev->dev, "Could not create sysfs file\n");
	if (device_create_file(&pdev->dev, &dev_attr_shutter_btn))
		dev_err(&pdev->dev, "Could not create sysfs file\n");

	dev_info(&pdev->dev, "Camera button driver initialized\n");

	return 0;

err_input_reg:
	input_free_device(button->input);

err_input_alloc:
	dev_set_drvdata(&pdev->dev, NULL);
	if (pdata->focus)
		free_irq(gpio_to_irq(pdata->focus), button);

err_irq_focus:
	if (pdata->focus)
		gpio_free(pdata->focus);

err_focus:
	free_irq(gpio_to_irq(button->shutter), button);

err_irq_shutter:
	gpio_free(pdata->shutter);

err_shutter:
err_pdata:
	kfree(button);

err_alloc:
	return err;
}

static int __exit rx51_camera_btn_remove(struct platform_device *pdev)
{
	struct rx51_cam_button *button = dev_get_drvdata(&pdev->dev);

	free_irq(gpio_to_irq(button->shutter), button);
	gpio_free(button->shutter);

	if (button->focus) {
		free_irq(gpio_to_irq(button->focus), button);
		gpio_free(button->focus);
	}
	device_remove_file(&pdev->dev, &dev_attr_focus_btn);
	device_remove_file(&pdev->dev, &dev_attr_shutter_btn);

	input_unregister_device(button->input);
	input_free_device(button->input);

	return 0;
}

static struct platform_driver rx51_cam_button_driver = {
	.probe		= rx51_camera_btn_probe,
	.remove		= __exit_p(rx51_camera_btn_remove),
	.driver		= {
		.name	= "camera_button",
		.owner	= THIS_MODULE,
	},
};

static int __init rx51_camera_btn_init(void)
{
	return platform_driver_register(&rx51_cam_button_driver);
}
module_init(rx51_camera_btn_init);

static void __exit rx51_camera_btn_exit(void)
{
	platform_driver_unregister(&rx51_cam_button_driver);
}
module_exit(rx51_camera_btn_exit);

MODULE_ALIAS("platform:camera_button");
MODULE_DESCRIPTION("Rx-51 Camera Button");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nokia Corporation");
