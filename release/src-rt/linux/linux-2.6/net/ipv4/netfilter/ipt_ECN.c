/* iptables module for the IPv4 and TCP ECN bits, Version 1.5
 *
 * (C) 2002 by Harald Welte <laforge@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/in.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/tcp.h>
#include <net/checksum.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_ECN.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables ECN modification module");

/* set ECT codepoint from IP header.
 * 	return 0 if there was an error. */
static inline int
set_ect_ip(struct sk_buff *skb, const struct ipt_ECN_info *einfo)
{
	struct iphdr *iph = ip_hdr(skb);

	if ((iph->tos & IPT_ECN_IP_MASK) != (einfo->ip_ect & IPT_ECN_IP_MASK)) {
		__u8 oldtos;
		if (!skb_make_writable(skb, sizeof(struct iphdr)))
			return 0;
		iph = ip_hdr(skb);
		oldtos = iph->tos;
		iph->tos &= ~IPT_ECN_IP_MASK;
		iph->tos |= (einfo->ip_ect & IPT_ECN_IP_MASK);
		nf_csum_replace2(&iph->check, htons(oldtos), htons(iph->tos));
	}
	return 1;
}

/* Return 0 if there was an error. */
static inline int
set_ect_tcp(struct sk_buff *skb, const struct ipt_ECN_info *einfo)
{
	struct tcphdr _tcph, *tcph;
	__be16 oldval;

	/* Not enought header? */
	tcph = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(_tcph), &_tcph);
	if (!tcph)
		return 0;

	if ((!(einfo->operation & IPT_ECN_OP_SET_ECE) ||
	     tcph->ece == einfo->proto.tcp.ece) &&
	    ((!(einfo->operation & IPT_ECN_OP_SET_CWR) ||
	     tcph->cwr == einfo->proto.tcp.cwr)))
		return 1;

	if (!skb_make_writable(skb, ip_hdrlen(skb) + sizeof(*tcph)))
		return 0;
	tcph = (void *)ip_hdr(skb) + ip_hdrlen(skb);

	oldval = ((__be16 *)tcph)[6];
	if (einfo->operation & IPT_ECN_OP_SET_ECE)
		tcph->ece = einfo->proto.tcp.ece;
	if (einfo->operation & IPT_ECN_OP_SET_CWR)
		tcph->cwr = einfo->proto.tcp.cwr;

	nf_proto_csum_replace2(&tcph->check, skb,
				oldval, ((__be16 *)tcph)[6], 0);
	return 1;
}

static unsigned int
target(struct sk_buff *skb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const struct xt_target *target,
       const void *targinfo)
{
	const struct ipt_ECN_info *einfo = targinfo;

	if (einfo->operation & IPT_ECN_OP_SET_IP)
		if (!set_ect_ip(skb, einfo))
			return NF_DROP;

	if (einfo->operation & (IPT_ECN_OP_SET_ECE | IPT_ECN_OP_SET_CWR)
	    && ip_hdr(skb)->protocol == IPPROTO_TCP)
		if (!set_ect_tcp(skb, einfo))
			return NF_DROP;

	return XT_CONTINUE;
}

static int
checkentry(const char *tablename,
	   const void *e_void,
	   const struct xt_target *target,
	   void *targinfo,
	   unsigned int hook_mask)
{
	const struct ipt_ECN_info *einfo = (struct ipt_ECN_info *)targinfo;
	const struct ipt_entry *e = e_void;

	if (einfo->operation & IPT_ECN_OP_MASK) {
		printk(KERN_WARNING "ECN: unsupported ECN operation %x\n",
			einfo->operation);
		return 0;
	}
	if (einfo->ip_ect & ~IPT_ECN_IP_MASK) {
		printk(KERN_WARNING "ECN: new ECT codepoint %x out of mask\n",
			einfo->ip_ect);
		return 0;
	}
	if ((einfo->operation & (IPT_ECN_OP_SET_ECE|IPT_ECN_OP_SET_CWR))
	    && (e->ip.proto != IPPROTO_TCP || (e->ip.invflags & XT_INV_PROTO))) {
		printk(KERN_WARNING "ECN: cannot use TCP operations on a "
		       "non-tcp rule\n");
		return 0;
	}
	return 1;
}

static struct xt_target ipt_ecn_reg = {
	.name		= "ECN",
	.family		= AF_INET,
	.target		= target,
	.targetsize	= sizeof(struct ipt_ECN_info),
	.table		= "mangle",
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init ipt_ecn_init(void)
{
	return xt_register_target(&ipt_ecn_reg);
}

static void __exit ipt_ecn_fini(void)
{
	xt_unregister_target(&ipt_ecn_reg);
}

module_init(ipt_ecn_init);
module_exit(ipt_ecn_fini);
