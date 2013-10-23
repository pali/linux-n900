/*
 * fbus-net.c -- Phonet over Fast Bus line discipline
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
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_phonet.h>
#include <linux/tty.h>

#include <net/sock.h>
#include <linux/phonet.h>
#include <net/phonet/phonet.h>

#include "fbus.h"

#define PN_ACK		0x7F
#define FBUS_MAX_MTU	30606 /* 6 bytes header, 255 segments of 120 bytes */
#define FBUS_TIMEOUT	(2 * HZ / 5) /* 400ms retransmission timeout */
#define FBUS_RETRIES	3	/* Retransmissin attempts */

/* Bit masks for the "sequence and ack byte" */
#define FBUS_SEQ	0x07	/* Sequence number mask */
/* For fragments only: */
/* Unused 		0x18 */
#define FBUS_RESET	0x20	/* Protocol reset */
#define FBUS_FIRST	0x40	/* First fragment of a message */
#define FBUS_NO_ACK	0x80	/* Do not ack this fragment */
/* For acknowledgements only: */
/* Unused		0x78 */
#define FBUS_BAD_SEQ	0x80	/* Sequence error in acknowledged fragment */

struct fbus_dev {
	struct tty_struct	*tty;

	struct sk_buff 		*rx_skb;
	struct sk_buff		**pnext;
	/* TX state belongs to the net TX queue when the queue is active.
	 * Otherwise, it is locked by RX callback and retransmit timer. */
	struct sk_buff		*tx_skb;
	struct timer_list	tx_timeout;
	unsigned int		tx_offset;
	spinlock_t		tx_lock;
	u8			tx_retries;
	u8			tx_seq;	/* TX sequence number */
	u8			rx_seq; /* RX sequence number */
	u8			rx_frags; /* RX remaining fragments */
};

static int fbus_tx_data(struct net_device *dev);

/*
 * Network device callbacks
 */
static netdev_tx_t fbus_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct fbus_dev *fbus = netdev_priv(dev);
	struct phonethdr *ph;
	int pnlen;

	/* FBUS is violating the upper network layer. Filter out packets that
	 * would screw the FBUS state machine up: */
	if (skb->protocol != htons(ETH_P_PHONET) || !pskb_may_pull(skb, 6))
		goto drop;

	ph = pn_hdr(skb);
	pnlen = 6 + ntohs(ph->pn_length);
	if (ph->pn_res == PN_ACK || pnlen > skb->len || pskb_trim(skb, pnlen))
		goto drop;

	WARN_ON(fbus->tx_skb);
	fbus->tx_skb = skb;
	fbus->tx_offset = 6;
	fbus->tx_retries = FBUS_RETRIES;
	fbus_tx_data(dev);
	netif_stop_queue(dev);
	return NETDEV_TX_OK;

drop:
	dev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void fbus_net_destruct(struct net_device *dev)
{
	struct fbus_dev *fbus = netdev_priv(dev);

	dev_kfree_skb(fbus->rx_skb);
	free_netdev(dev);
}

static int fbus_net_open(struct net_device *dev)
{
	struct fbus_dev *fbus = netdev_priv(dev);

	start_tty(fbus->tty);
	netif_wake_queue(dev);
	return 0;
}

static int fbus_net_close(struct net_device *dev)
{
	struct fbus_dev *fbus = netdev_priv(dev);
	struct sk_buff *skb;

	netif_stop_queue(dev);
	del_timer_sync(&fbus->tx_timeout);
	stop_tty(fbus->tty);

	spin_lock_bh(&fbus->tx_lock);
	skb = fbus->tx_skb;
	fbus->tx_skb = NULL;
	spin_unlock_bh(&fbus->tx_lock);
	if (unlikely(skb)) {
		dev_kfree_skb(skb);
		dev->stats.tx_errors++;
		dev->stats.tx_aborted_errors++;
	}
	return 0;
}

static int fbus_net_set_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < PHONET_MIN_MTU || new_mtu > FBUS_MAX_MTU)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops fbus_netdev_ops = {
	.ndo_open = fbus_net_open,
	.ndo_stop = fbus_net_close,
	.ndo_start_xmit = fbus_net_xmit,
	.ndo_change_mtu = fbus_net_set_mtu,
};

static void fbus_net_setup(struct net_device *dev)
{
	dev->features		= NETIF_F_SG | NETIF_F_NO_CSUM
				| NETIF_F_FRAGLIST;
	dev->netdev_ops		= &fbus_netdev_ops;
	dev->header_ops		= &phonet_header_ops;
	dev->type		= ARPHRD_PHONET;
	dev->flags		= IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu		= FBUS_MAX_MTU;
	dev->hard_header_len	= 1;
	dev->dev_addr[0]	= PN_MEDIA_FBUS;
	dev->addr_len		= 1;
	dev->tx_queue_len	= 1;

	dev->destructor		= fbus_net_destruct;
}

/* FBUS ACK message format */
struct fbus_ack {
	u8	fa_media;
	u8	fa_src;
	u8	fa_dst;
	u8	fa_ack;
	u16	fa_len;
	u8	fa_func;
	u8	fa_seq;
};

/*
 * Internal functions
 */

/* Transmitted Phonet packets are fragmented into FBUS frames,
 * no more than 16-bits aligned up to 130 bytes each.
 * Each FBUS frames contains a copy of the four first byte
 * of the original Phonet header, then a 16-bits frame payload
 * bytes length (no more than 120, so high-order byte is nul),
 * then up to 120 bytes from the Phonet packet payload.
 * All frames but the last one must carry exactly 120 bytes.
 * The FBUS protocol adds fragmentation footer (2 bytes).
 * Then a nul pad byte is appended if the payload length is odd.
 * Finally, we have the 16-bits XOR sum of the entire frame.
 *
 * Called with TX lock or from xmit queue.
 */
static int fbus_tx_data(struct net_device *dev)
{
	struct fbus_dev *fbus = netdev_priv(dev);
	struct sk_buff *skb, *frag;
	int offset, len;
	u8 seqack;

	skb = fbus->tx_skb;
	offset = fbus->tx_offset;
	len = skb->len - offset;
	if (len <= 0) {
		fbus->tx_skb = NULL;
		netif_wake_queue(dev);
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += skb->len;
		dev_kfree_skb(skb);
		return 0;
	}

	/* This functions adds a Phonet header and 2-bytes footer.
	 * fbus_xmit() pads to word, and adds 2-bytes sum. */
	frag = alloc_skb(10 + len + (len & 1), GFP_ATOMIC);
	if (!frag)
		return -ENOMEM;

	/* Fragment header (pulled in fbus_net_xmit()) */
	skb_copy_from_linear_data(skb, skb_put(frag, 4), 4);

	/* Fragment length */
	if (len > FBUS_MAX_FRAG)
		len = FBUS_MAX_FRAG;
	*(u16 *)skb_put(frag, 2) = htons(len + 2);

	/* Fragment payload */
	if (skb_copy_bits(skb, offset, skb_put(frag, len), len))
		BUG();

	/* Fragment index, numbered from N down to 1 */
	*skb_put(frag, 1) = (len + FBUS_MAX_FRAG - 1) / FBUS_MAX_FRAG;

	/* Sequence and ack byte */
	seqack = fbus->tx_seq;
	if (offset == 6)
		seqack |= FBUS_FIRST; /* First fragment of packet */
	*skb_put(frag, 1) = seqack;

	fbus->tx_timeout.expires = jiffies + FBUS_TIMEOUT;
	add_timer(&fbus->tx_timeout);
	return fbus_xmit(frag, fbus->tty);
}

/* Transmission acknowledgement timeout */
static void fbus_tx_timeout(unsigned long data)
{
	struct net_device *dev = (void *)data;
	struct fbus_dev *fbus = netdev_priv(dev);
	struct sk_buff *skb = NULL;

	spin_lock(&fbus->tx_lock);
	if (!netif_queue_stopped(dev))
		goto out; /* Lost race against ACK of last fragment. */

	if (!--fbus->tx_retries) {
		skb = fbus->tx_skb; /* Too many failed attempts */
		fbus->tx_skb = NULL;
	} else
		fbus_tx_data(dev); /* Retry */
out:
	spin_unlock(&fbus->tx_lock);

	if (skb) {
		printk(KERN_ERR"%s: transmission failed. Link is dead?\n",
			dev->name);
		netif_carrier_off(dev);
		dev->stats.tx_errors++;
		dev->stats.tx_heartbeat_errors++;
		dev_kfree_skb(skb);
	}
}


/* Format an outgoing acknowledgement. */
static int fbus_tx_ack(struct sk_buff *oskb, struct net_device *dev, u8 byte)
{
	struct fbus_dev *fbus = netdev_priv(dev);
	struct sk_buff *skb;
	struct fbus_ack *ack;
	const struct phonethdr *ph = pn_hdr(oskb);

	/* Pre-allocate two more bytes for low-layer checksum */
	skb = alloc_skb(sizeof(*ack) + 2, GFP_ATOMIC);
	if (unlikely(!skb))
		return -ENOMEM;

	ack = (struct fbus_ack *)skb_put(skb, sizeof(*ack));
	ack->fa_media = dev->dev_addr[0];
	ack->fa_src = ph->pn_sdev;
	ack->fa_dst = ph->pn_rdev;
	ack->fa_ack = PN_ACK;
	ack->fa_len = htons(2);
	ack->fa_seq = byte;
	return fbus_xmit(skb, fbus->tty);
}

/* Parse an incoming acknowledgement. */
static void fbus_rx_ack(struct net_device *dev, struct sk_buff *skb)
{
	struct fbus_dev *fbus = netdev_priv(dev);
	struct sk_buff *oskb;
	const struct phonethdr *ph = pn_hdr(skb);
	const struct phonethdr *oph;

	if (ntohs(ph->pn_length) != 2 || /* Invalid ACK format */
			!netif_queue_stopped(dev)) /* No TX pending ACK */
		return;

	spin_lock_bh(&fbus->tx_lock);
	oskb = fbus->tx_skb;
	if (unlikely(!oskb))
		goto out; /* Lost race with dev_close() */

	/* Is this a valid acknoledgment? */
	oph = pn_hdr(oskb); /* Pulled in xmit callback */
	if (ph->pn_rdev != oph->pn_sdev ||
		ph->pn_sdev != oph->pn_rdev ||
		skb->data[5] != oph->pn_res ||
		skb->data[6] != (fbus->tx_seq & FBUS_SEQ))
		goto out;

	del_timer(&fbus->tx_timeout);
	/* We ignore the bad sequence bit. It is set when a retransmit
	 * occurs due to loss of ACK. */
	fbus->tx_seq++;
	fbus->tx_seq &= FBUS_SEQ;
	fbus->tx_offset += FBUS_MAX_FRAG;
	fbus_tx_data(dev);
out:
	spin_unlock_bh(&fbus->tx_lock);
}

/* Discard an incomplete FBUS packet */
static void fbus_rx_discard(struct net_device *dev)
{
	struct fbus_dev *fbus = netdev_priv(dev);

	if (fbus->rx_skb) {
		dev_kfree_skb(fbus->rx_skb);
		fbus->rx_skb = NULL;
		dev->stats.rx_missed_errors++;
		dev->stats.rx_errors++;
	}
}

/* Receive a FBUS frame (= fragment of Phonet packet) */
void fbus_rx(struct sk_buff *skb, struct net_device *dev)
{
	struct fbus_dev *fbus = netdev_priv(dev);
	struct phonethdr *ph = pn_hdr(skb);
	int len = ntohs(ph->pn_length);
	u8 index, byte;

	/* FBUS frames are made of:
	 *  - the original Phonet header, with a modified length field,
	 *  - up to 120 bytes of payload,
	 *  - 2 bytes for fragmentation handling,
	 *  - an filler byte if the payliad length is odd (already discarded),
	 *  - 2 bytes of XOR checksum (already discarded by lower-layer).
	 */

	if (len < 2 || ((len + 5 /*= sizeof(*ph)*/) > skb->len)) {
		dev->stats.rx_length_errors++;
		goto drop;
	}

	/* Handle acknowledgement */
	/* FBUS uses a special Phonet(!) resource field value for this.
	 * Ugly but working as long as only link-layers use that value. */
	if (ph->pn_res == PN_ACK) {
		fbus_rx_ack(dev, skb);
		goto drop;
	}

	skb_copy_from_linear_data_offset(skb, len + 3, &index, 1);
	if (index == 0) {
		dev->stats.rx_crc_errors++;
		goto drop;
	}

	skb_copy_from_linear_data_offset(skb, len + 4, &byte, 1);
	len -= 2;

	/* Check sequence number */
	if (byte & FBUS_RESET)
		fbus->rx_seq = byte & FBUS_SEQ;
	else if (fbus->rx_seq != (byte & FBUS_SEQ)) {
		if (!(byte & FBUS_NO_ACK))
			fbus_tx_ack(skb, dev, FBUS_BAD_SEQ|(byte & FBUS_SEQ));
		dev->stats.rx_missed_errors++;
		goto drop;
	}
	fbus->rx_seq++;
	fbus->rx_seq &= FBUS_SEQ;

	/* Send acknowledgement */
	if (!(byte & FBUS_NO_ACK))
		fbus_tx_ack(skb, dev, byte & FBUS_SEQ);

	/* Check index */
	if (byte & FBUS_FIRST) {
		fbus->rx_frags = index;
		fbus_rx_discard(dev);
	} else if (fbus->rx_frags != index)
		goto drop;
	WARN_ON(!fbus->rx_frags);
	fbus->rx_frags--;

	/* Re-assemble the original packet */
	if (!fbus->rx_skb) {
		/* First fragment of packet, keep the Phonet header */
		__skb_trim(skb, len + 5);
		fbus->rx_skb = skb;
		fbus->pnext = &(skb_shinfo(skb)->frag_list);
	} else {
		struct sk_buff *rskb = fbus->rx_skb;

		/* Remove header, append to existing buffer */
		__skb_pull(skb, 5);
		__skb_trim(skb, len);

		*(fbus->pnext) = skb;
		fbus->pnext = &(skb->next);
		rskb->len += skb->len;
		rskb->data_len += skb->len;
		rskb->truesize += skb->truesize;
		skb = rskb;
	}

	if (fbus->rx_frags == 0) {
		fbus->rx_skb = NULL;

		ph = pn_hdr(skb);
		ph->pn_length = htons(skb->len - 5);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += 1 + skb->len;
		netif_rx_ni(skb);
	}
	return;

drop:
	dev_kfree_skb(skb);
}

struct net_device *fbus_create(struct tty_struct *tty)
{
	static const char ifname[] = "fbus%d";
	struct net_device *dev;
	struct fbus_dev *fbus;
	int err;

	dev = alloc_netdev(sizeof(*fbus), ifname, fbus_net_setup);
	if (!dev)
		return ERR_PTR(ENOMEM);

	/*SET_NETDEV_DEV(dev, FIXME);*/
	netif_stop_queue(dev);
	fbus = netdev_priv(dev);
	fbus->tty = tty;
	init_timer(&fbus->tx_timeout);
	fbus->tx_timeout.function = fbus_tx_timeout;
	fbus->tx_timeout.data = (unsigned long)dev;
	spin_lock_init(&fbus->tx_lock);
	fbus->tx_seq = FBUS_RESET;

	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		return ERR_PTR(err);
	}
	return dev;
}

void fbus_destroy(struct net_device *dev)
{
	unregister_netdev(dev);
}
