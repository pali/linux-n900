/*
 * Generic driver for video bus switches
 *
 * Copyright (C) 2015 Sebastian Reichel <sre@kernel.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#define DEBUG

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/gpio/consumer.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-of.h>

/*
 * TODO:
 * isp_subdev_notifier_complete() calls v4l2_device_register_subdev_nodes()
 */

#define CSI_SWITCH_SUBDEVS 2
#define CSI_SWITCH_PORTS 3

enum vbs_state {
	CSI_SWITCH_DISABLED,
	CSI_SWITCH_PORT_1,
	CSI_SWITCH_PORT_2,
};

struct vbs_data {
	struct gpio_desc *swgpio;
	struct v4l2_subdev subdev;
	struct v4l2_async_notifier notifier;
	struct media_pad pads[CSI_SWITCH_PORTS];
	enum vbs_state state;
};

struct vbs_async_subdev {
	struct v4l2_subdev *sd;
	struct v4l2_async_subdev asd;
	u8 port;
};

static int vbs_of_parse_nodes(struct device *dev, struct vbs_data *pdata)
{
	struct v4l2_async_notifier *notifier = &pdata->notifier;
	struct device_node *node = NULL;

	notifier->subdevs = devm_kcalloc(dev, CSI_SWITCH_SUBDEVS,
		sizeof(*notifier->subdevs), GFP_KERNEL);
	if (!notifier->subdevs)
		return -ENOMEM;

	notifier->num_subdevs = 0;
	while (notifier->num_subdevs < CSI_SWITCH_SUBDEVS &&
	       (node = of_graph_get_next_endpoint(dev->of_node, node))) {
		struct v4l2_of_endpoint vep;
		struct vbs_async_subdev *ssd;

		/* skip first port (connected to isp) */
		v4l2_of_parse_endpoint(node, &vep);
		if (vep.base.port == 0) {
			struct device_node *ispnode;

			ispnode = of_graph_get_remote_port_parent(node);
			if (!ispnode) {
				dev_warn(dev, "bad remote port parent\n");
				return -EINVAL;
			}

			of_node_put(node);
			continue;
		}

		ssd = devm_kzalloc(dev, sizeof(*ssd), GFP_KERNEL);
		if (!ssd) {
			of_node_put(node);
			return -ENOMEM;
		}

		ssd->port = vep.base.port;

		notifier->subdevs[notifier->num_subdevs] = &ssd->asd;

		ssd->asd.match.of.node = of_graph_get_remote_port_parent(node);
		of_node_put(node);
		if (!ssd->asd.match.of.node) {
			dev_warn(dev, "bad remote port parent\n");
			return -EINVAL;
		}

		ssd->asd.match_type = V4L2_ASYNC_MATCH_OF;
		notifier->num_subdevs++;
	}

	return notifier->num_subdevs;
}

static int vbs_registered(struct v4l2_subdev *sd)
{
	struct v4l2_device *v4l2_dev = sd->v4l2_dev;
	struct vbs_data *pdata;
	int err;

	dev_dbg(sd->dev, "registered, init notifier...\n");

	pdata = v4l2_get_subdevdata(sd);

	err = v4l2_async_notifier_register(v4l2_dev, &pdata->notifier);
	if (err)
		return err;

	return 0;
}

static int vbs_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vbs_data *pdata = v4l2_get_subdevdata(sd);
	bool enable = flags & MEDIA_LNK_FL_ENABLED;
	struct v4l2_async_subdev *asd;
	struct vbs_async_subdev *ssd;

	if (local->index > CSI_SWITCH_PORTS-1)
		return -ENXIO;

	/* no configuration needed on source port */
	if (local->index == 0)
		return 0;

	if (!enable) {
		if (local->index == pdata->state) {
			pdata->state = CSI_SWITCH_DISABLED;
			return 0;
		} else {
			return -EINVAL;
		}
	}

	/* there can only be one active sink at the same time */
	if (pdata->state != CSI_SWITCH_DISABLED)
		return -EBUSY;

	switch (local->index) {
	case 1:
		pdata->state = CSI_SWITCH_PORT_1;
		gpiod_set_value(pdata->swgpio, false);

		asd = pdata->notifier.subdevs[0];
		ssd = container_of(asd, struct vbs_async_subdev, asd);
		pdata->subdev.ctrl_handler = ssd->sd->ctrl_handler;
		break;
	case 2:
		pdata->state = CSI_SWITCH_PORT_2;
		gpiod_set_value(pdata->swgpio, true);

		asd = pdata->notifier.subdevs[1];
		ssd = container_of(asd, struct vbs_async_subdev, asd);
		pdata->subdev.ctrl_handler = ssd->sd->ctrl_handler;
		break;
	}

	return 0;
}

static int vbs_subdev_notifier_bound(struct v4l2_async_notifier *async,
				     struct v4l2_subdev *subdev,
				     struct v4l2_async_subdev *asd)
{
	struct vbs_data *pdata = container_of(async,
		struct vbs_data, notifier);
	struct vbs_async_subdev *ssd =
		container_of(asd, struct vbs_async_subdev, asd);
	struct media_entity *sink = &pdata->subdev.entity;
	struct media_entity *src = &subdev->entity;
	int sink_pad = ssd->port;
	int src_pad;
	int err;

	if (sink_pad >= sink->num_pads) {
		dev_err(pdata->subdev.dev, "no sink pad in internal entity!\n");
		return -EINVAL;
	}

	for (src_pad = 0; src_pad < subdev->entity.num_pads; src_pad++) {
		if (subdev->entity.pads[src_pad].flags & MEDIA_PAD_FL_SOURCE)
			break;
	}

	if (src_pad >= src->num_pads) {
		dev_err(pdata->subdev.dev, "no source pad in external entity\n");
		return -EINVAL;
	}

	dev_dbg(pdata->subdev.dev, "create link: %s -> %s\n", src->name, sink->name);
	err = media_entity_create_link(src, src_pad, sink, sink_pad, 0);
	if (err < 0)
		return err;

	ssd->sd = subdev;

	return err;
}

static int vbs_subdev_notifier_complete(struct v4l2_async_notifier *async)
{
	struct vbs_data *pdata = container_of(async, struct vbs_data, notifier);

	return v4l2_device_register_subdev_nodes(pdata->subdev.v4l2_dev);
}

static const struct v4l2_subdev_internal_ops vbs_internal_ops = {
	.registered = &vbs_registered,
};

static const struct media_entity_operations vbs_media_ops = {
	.link_setup = vbs_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_ops vbs_ops = {
	/* empty, since only media entity operations are needed */
};

static int video_bus_switch_probe(struct platform_device *pdev)
{
	struct vbs_data *pdata;
	int err = 0;

	/* platform data */
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, pdata);

	/* switch gpio */
	pdata->swgpio = devm_gpiod_get(&pdev->dev, "switch", GPIOD_OUT_HIGH);
	if (IS_ERR(pdata->swgpio)) {
		err = PTR_ERR(pdata->swgpio);
		dev_err(&pdev->dev, "Failed to request gpio: %d\n", err);
		return err;
	}

	/* find sub-devices */
	err = vbs_of_parse_nodes(&pdev->dev, pdata);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to parse nodes: %d\n", err);
		return err;
	}

	pdata->state = CSI_SWITCH_DISABLED;
	pdata->notifier.bound = vbs_subdev_notifier_bound;
	pdata->notifier.complete = vbs_subdev_notifier_complete;

	/* setup subdev */
	pdata->pads[0].flags = MEDIA_PAD_FL_SOURCE;
	pdata->pads[1].flags = MEDIA_PAD_FL_SINK;
	pdata->pads[2].flags = MEDIA_PAD_FL_SINK;

	v4l2_subdev_init(&pdata->subdev, &vbs_ops);
	pdata->subdev.dev = &pdev->dev;
	pdata->subdev.owner = pdev->dev.driver->owner;
	strncpy(pdata->subdev.name, dev_name(&pdev->dev), V4L2_SUBDEV_NAME_SIZE);
	v4l2_set_subdevdata(&pdata->subdev, pdata);
	pdata->subdev.entity.flags |= MEDIA_ENT_T_V4L2_SUBDEV_SWITCH;
	pdata->subdev.entity.ops = &vbs_media_ops;
	pdata->subdev.internal_ops = &vbs_internal_ops;
	err = media_entity_init(&pdata->subdev.entity, CSI_SWITCH_PORTS,
				pdata->pads, 0);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to init media entity: %d\n", err);
		return err;
	}

	/* register subdev */
	err = v4l2_async_register_subdev(&pdata->subdev);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to register v4l2 subdev: %d\n", err);
		media_entity_cleanup(&pdata->subdev.entity);
		return err;
	}

	return 0;
}

static int video_bus_switch_remove(struct platform_device *pdev)
{
	struct vbs_data *pdata = platform_get_drvdata(pdev);

	v4l2_async_notifier_unregister(&pdata->notifier);
	v4l2_async_unregister_subdev(&pdata->subdev);
	media_entity_cleanup(&pdata->subdev.entity);

	return 0;
}

static const struct of_device_id video_bus_switch_of_match[] = {
	{ .compatible = "video-bus-switch", },
}
MODULE_DEVICE_TABLE(of, video_bus_switch_of_match);

static struct platform_driver video_bus_switch_driver = {
	.driver = {
		.name	= "video-bus-switch",
		.of_match_table = video_bus_switch_of_match,
	},
	.probe		= video_bus_switch_probe,
	.remove		= video_bus_switch_remove,
};

module_platform_driver(video_bus_switch_driver);

MODULE_AUTHOR("Sebastian Reichel <sre@kernel.org>");
MODULE_DESCRIPTION("Video Bus Switch");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:video-bus-switch");
