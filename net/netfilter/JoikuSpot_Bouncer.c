/*
 * Implementation of JoikuSpotBouncer module
 * Copyright (C) 2011  JoikuSoft Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/netfilter_ipv4.h>
#include <net/udp.h>
#include <net/netfilter/nf_tproxy_core.h>

#define JOIKU_CLASS_A_MASK 0x000000ff
#define JOIKU_LOOPBACK 0x0000007f

static unsigned int joikuspot_nf_hook(unsigned int hook,
				      struct sk_buff *pskb,
				      const struct net_device *in,
				      const struct net_device *out,
				      int(*okfn)(struct sk_buff *))
{
	struct sock *sk = NULL;
	struct iphdr *iph = ipip_hdr(pskb);


	if (iph->version != IPVERSION ||
	    (iph->daddr & JOIKU_CLASS_A_MASK) == JOIKU_LOOPBACK)
		return NF_ACCEPT;

	if (iph->protocol == IPPROTO_TCP) {
		struct tcphdr *th, tcph;

		th = skb_header_pointer(pskb, iph->ihl << 2, sizeof(tcph),
					&tcph);
		if (!th)
			return NF_ACCEPT;
		sk = inet_lookup(dev_net(skb_dst(pskb)->dev),
				 &tcp_hashinfo, iph->saddr, th->source,
				 iph->daddr, th->dest, inet_iif(pskb));
	} else if (iph->protocol == IPPROTO_UDP) {
		struct udphdr *uh, udph;

		uh = skb_header_pointer(pskb, iph->ihl << 2, sizeof(udph),
					&udph);
		if (!uh)
			return NF_ACCEPT;
		sk = udp4_lib_lookup(dev_net(skb_dst(pskb)->dev),
				     iph->saddr, uh->source, iph->daddr,
				     uh->dest, inet_iif(pskb));
	} else {
		return NF_ACCEPT;
	}

	if (!sk) {
		return NF_DROP;
	} else {
		sock_put(sk);
		return NF_ACCEPT;
	}
}

static struct nf_hook_ops joikuspot_ops = {
	.hook = joikuspot_nf_hook,
	.owner = THIS_MODULE,
	.pf = PF_INET,
	.hooknum = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FIRST
};

static int __init joikuspot_init(void)
{
	return nf_register_hook(&joikuspot_ops);
}
module_init(joikuspot_init);

static void __exit joikuspot_exit(void)
{
	nf_unregister_hook(&joikuspot_ops);
}
module_exit(joikuspot_exit);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("JoikuSoft Oy Ltd <info@joikusoft.com>");
