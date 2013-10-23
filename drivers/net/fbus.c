/*
 * n_fbus.c -- Phonet over Fast Bus line discipline
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *
 * Author: Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/poll.h>
#include <linux/netdevice.h>
#include <linux/phonet.h>

#include "fbus.h"


struct fbus_disc {
	struct net_device	*dev;
	struct sk_buff		*rx, *tx;
	struct sk_buff_head	queue;
};

/*
 * Line discipline user callbacks
 */
static int fbus_open(struct tty_struct *tty)
{
	struct net_device *dev;
	struct fbus_disc *fbus;

	if (tty->ops->write == NULL)
		return -EOPNOTSUPP;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	fbus = kzalloc(sizeof(*fbus), GFP_KERNEL);
	if (!fbus)
		return -ENOMEM;

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	stop_tty(tty);

	dev = fbus_create(tty);
	if (IS_ERR(dev)) {
		kfree(fbus);
		return PTR_ERR(dev);
	}

	fbus->dev = dev;
	skb_queue_head_init(&fbus->queue);

	tty->disc_data = fbus;
	tty->receive_room = 65536;
	return 0;
}

static void fbus_close(struct tty_struct *tty)
{
	struct fbus_disc *fbus = tty->disc_data;

	tty->disc_data = NULL;

	fbus_destroy(fbus->dev);
	dev_kfree_skb(fbus->rx);
	dev_kfree_skb(fbus->tx);
	skb_queue_purge(&fbus->queue);
	kfree(fbus);
}

static int fbus_ioctl(struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct fbus_disc *fbus = tty->disc_data;
	int val;

	switch (cmd) {
	case FIONREAD:
		val = 0;
		return put_user(val, (int __user *)arg) ? -EFAULT : 0;

	case FBUS_GETIFINDEX:
		val = fbus->dev->ifindex;
		return put_user(val, (int __user *)arg) ? -EFAULT : 0;

	default:
		return tty_mode_ioctl(tty, file, cmd, arg);
	}
}

static unsigned int fbus_poll(struct tty_struct *tty, struct file *file,
				poll_table *wait)
{
	struct fbus_disc *fbus = tty->disc_data;

	poll_wait(file, &tty->write_wait, wait);
	return netif_carrier_ok(fbus->dev) ? 0 : POLLHUP;
}

static int fbus_hangup(struct tty_struct *tty)
{
	struct fbus_disc *fbus = tty->disc_data;

	netif_carrier_off(fbus->dev);
	return 0;
}

static uint16_t fbus_csum(const uint8_t *data, size_t len)
{
	uint64_t xor64 = 0;
	uint32_t xor;

	WARN_ON(((unsigned long)data) & 7); /* unaligned?! */
	while (len >= 8) {
		xor64 ^= *(const uint64_t *)data;
		len -= 8;
		data += 8;
	}

	xor = xor64 ^ (xor64 >> 32);
	if (len >= 4) {
		xor ^= *(const uint32_t *)data;
		len -= 4;
		data += 4;
	}

	xor ^= (xor >> 16);
	if (len >= 2)
		xor ^= *(const uint16_t *)data;

	WARN_ON(len & 1);
	return xor;
}

/*
 * Line discipline driver callbacks
 */
static int fbus_overflow(struct tty_struct *tty, const unsigned char *data,
				char *fp, int count)
{
	struct fbus_disc *fbus = tty->disc_data;
	struct net_device *dev = fbus->dev;

	while (count > 0) {
		struct sk_buff *skb = fbus->rx;
		u16 fragsize, fraglen;

		/* Allocate memory for fragment */
		if (!skb) {
			skb = netdev_alloc_skb(dev, 130);
			if (!skb) {
				dev->stats.rx_over_errors++;
				goto fail;
			}
			fbus->rx = skb;
		}

		/* Use FBUS media type as a sync byte */
		if (skb->len == 0) {
			unsigned char *sync;

			sync = memchr(data, PN_MEDIA_FBUS, count);
			if (!sync)
				break;
			count -= (sync - data);
			data = sync;
		}

		/* Get FBUS fragment header */
		if (skb->len < 6) {
			unsigned cp = 6 - skb->len;

			if ((unsigned)count < cp)
				cp = count;
			memcpy(__skb_put(skb, cp), data, cp);
			data += cp;
			count -= cp;
			if (skb->len < 6)
				break;
		}

		skb_copy_from_linear_data_offset(skb, 4, &fraglen, 2);
		fraglen = ntohs(fraglen);
		if (fraglen > (FBUS_MAX_FRAG + 2)) {
			dev->stats.rx_length_errors++;
			goto fail;
		}
		fragsize = 6 + fraglen + (fraglen & 1) + 2;

		/* Copy payload data, padding and checksum */
		if (skb->len < fragsize) {
			unsigned cp = fragsize - skb->len;

			if ((unsigned)count < cp)
				cp = count;
			memcpy(__skb_put(skb, cp), data, cp);
			data += cp;
			count -= cp;

			if (skb->len < fragsize)
				break;
		}

		/* Checksum */
		if (fbus_csum(skb->data, skb->len)) {
			printk(KERN_ERR"bad checksum\n");
			dev->stats.rx_crc_errors++;
			goto fail;
		}
		__skb_trim(skb, 6 + fraglen); /* discard padding, checksum */

		fbus->rx = NULL;
		skb->protocol = htons(ETH_P_PHONET);
		skb_reset_mac_header(skb);
		__skb_pull(skb, 1);
		skb_reset_network_header(skb);
		skb->dev = dev;
		fbus_rx(skb, dev);
		continue;
fail:
		dev->stats.rx_errors++;
		if (likely(skb) && skb->len > 0) {
			unsigned char *sync;
			int len;

			sync = memchr(skb->data + 1, PN_MEDIA_FBUS,
					skb->len - 1);
			if (sync) {	/* Another sync attempt */
				len = 6 - (sync - skb->data);
				memmove(skb->data, sync, len);
			} else
				len = 0;
			__skb_trim(skb, len);
		}
	}

	return 0;
}

static void fbus_underflow(struct tty_struct *tty)
{
	struct fbus_disc *fbus = tty->disc_data;

	/* Use TTY_DO_WRITE_WAKEUP as a try-lock */
	if (!test_and_clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags))
		return;

	for (;;) {
		struct sk_buff *skb;
		int len;

		skb = fbus->tx;
		if (!skb) {
			skb = skb_dequeue(&fbus->queue);
			if (!skb)
				break;
			fbus->tx = skb;
		}

		if (skb->len > 0) {
			len = tty->ops->write(tty, skb->data, skb->len);
			if (len == 0) {
				set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
				break;
			}
			__skb_pull(skb, len);
		}

		if (skb->len == 0) {
			fbus->tx = NULL;
			dev_kfree_skb(skb);
		}
	}
}

static struct tty_ldisc_ops fbus_ops = {
	.owner		= THIS_MODULE,
	.magic		= TTY_LDISC_MAGIC,
	.name		= "fbus",
	.open		= fbus_open,
	.close		= fbus_close,
	.ioctl		= fbus_ioctl,
	.poll		= fbus_poll,
	.hangup		= fbus_hangup,
	.receive_buf	= fbus_overflow,
	.write_wakeup	= fbus_underflow,
};

int fbus_xmit(struct sk_buff *skb, struct tty_struct *tty)
{
	struct fbus_disc *fbus = tty->disc_data;
	uint16_t csum;

	/* Padding */
	if (skb->len & 1)
		memset(skb_put(skb, 1), 0, 1);

	/* XOR sum */
	csum = fbus_csum(skb->data, skb->len);
	memcpy(skb_put(skb, 2), &csum, 2);

	skb_queue_tail(&fbus->queue, skb);
	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	fbus_underflow(tty);
	return 0;
}

static int __init fbus_init(void)
{
	return tty_register_ldisc(N_FBUS, &fbus_ops);
}

static void __exit fbus_exit(void)
{
	if (tty_unregister_ldisc(N_FBUS))
		BUG();
}

module_init(fbus_init);
module_exit(fbus_exit);

MODULE_AUTHOR("Remi Denis-Courmont");
MODULE_DESCRIPTION("Phonet FBUS serial line discipline");
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_FBUS);
