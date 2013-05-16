/*
 * firewall3 - 3rd OpenWrt UCI firewall implementation
 *
 *   Copyright (C) 2013 Jo-Philipp Wich <jow@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "iptables.h"


static struct option base_opts[] = {
	{ .name = "match",  .has_arg = 1, .val = 'm' },
	{ .name = "jump",   .has_arg = 1, .val = 'j' },
	{ .name = "append", .has_arg = 1, .val = 'A' },
	{ NULL }
};

static struct xtables_globals xtg = {
	.option_offset = 0,
	.program_version = "4",
	.orig_opts = base_opts,
};

static struct xtables_globals xtg6 = {
	.option_offset = 0,
	.program_version = "6",
	.orig_opts = base_opts,
};

/* Required by certain extensions like SNAT and DNAT */
int kernel_version;

void
get_kernel_version(void)
{
	static struct utsname uts;
	int x = 0, y = 0, z = 0;

	if (uname(&uts) == -1)
		sprintf(uts.release, "3.0.0");

	sscanf(uts.release, "%d.%d.%d", &x, &y, &z);
	kernel_version = LINUX_VERSION(x, y, z);
}

struct fw3_ipt_handle *
fw3_ipt_open(enum fw3_family family, enum fw3_table table)
{
	struct fw3_ipt_handle *h;

	h = fw3_alloc(sizeof(*h));

	xtables_init();

	if (family == FW3_FAMILY_V6)
	{
		h->family = FW3_FAMILY_V6;
		h->table  = table;
		h->handle = ip6tc_init(fw3_flag_names[table]);

		xtables_set_params(&xtg6);
		xtables_set_nfproto(NFPROTO_IPV6);
	}
	else
	{
		h->family = FW3_FAMILY_V4;
		h->table  = table;
		h->handle = iptc_init(fw3_flag_names[table]);

		xtables_set_params(&xtg);
		xtables_set_nfproto(NFPROTO_IPV4);
	}

	if (!h->handle)
	{
		free(h);
		return NULL;
	}

	xtables_pending_matches = NULL;
	xtables_pending_targets = NULL;

	xtables_matches = NULL;
	xtables_targets = NULL;

	init_extensions();
	init_extensions4();
	init_extensions6();

	return h;
}

void
fw3_ipt_set_policy(struct fw3_ipt_handle *h, const char *chain,
                   enum fw3_flag policy)
{
	if (h->family == FW3_FAMILY_V6)
		ip6tc_set_policy(chain, fw3_flag_names[policy], NULL, h->handle);
	else
		iptc_set_policy(chain, fw3_flag_names[policy], NULL, h->handle);
}

void
fw3_ipt_delete_chain(struct fw3_ipt_handle *h, const char *chain)
{
	if (fw3_pr_debug)
	{
		printf("-F %s\n", chain);
		printf("-X %s\n", chain);
	}

	if (h->family == FW3_FAMILY_V6)
	{
		if (ip6tc_flush_entries(chain, h->handle))
			ip6tc_delete_chain(chain, h->handle);
	}
	else
	{
		if (iptc_flush_entries(chain, h->handle))
			iptc_delete_chain(chain, h->handle);
	}
}

void
fw3_ipt_delete_rules(struct fw3_ipt_handle *h, const char *target)
{
	unsigned int num;
	const struct ipt_entry *e;
	const struct ip6t_entry *e6;
	const char *chain;
	const char *t;
	bool found;

	if (h->family == FW3_FAMILY_V6)
	{
		for (chain = ip6tc_first_chain(h->handle);
		     chain != NULL;
		     chain = ip6tc_next_chain(h->handle))
		{
			do {
				found = false;

				for (num = 0, e6 = ip6tc_first_rule(chain, h->handle);
					 e6 != NULL;
					 num++, e6 = ip6tc_next_rule(e6, h->handle))
				{
					t = ip6tc_get_target(e6, h->handle);

					if (*t && !strcmp(t, target))
					{
						if (fw3_pr_debug)
							printf("-D %s %u\n", chain, num + 1);

						ip6tc_delete_num_entry(chain, num, h->handle);
						found = true;
						break;
					}
				}
			} while (found);
		}
	}
	else
	{
		for (chain = iptc_first_chain(h->handle);
		     chain != NULL;
		     chain = iptc_next_chain(h->handle))
		{
			do {
				found = false;

				for (num = 0, e = iptc_first_rule(chain, h->handle);
				     e != NULL;
					 num++, e = iptc_next_rule(e, h->handle))
				{
					t = iptc_get_target(e, h->handle);

					if (*t && !strcmp(t, target))
					{
						if (fw3_pr_debug)
							printf("-D %s %u\n", chain, num + 1);

						iptc_delete_num_entry(chain, num, h->handle);
						found = true;
						break;
					}
				}
			} while (found);
		}
	}
}

void
fw3_ipt_flush(struct fw3_ipt_handle *h)
{
	const char *chain;

	if (h->family == FW3_FAMILY_V6)
	{
		for (chain = ip6tc_first_chain(h->handle);
		     chain != NULL;
		     chain = ip6tc_next_chain(h->handle))
		{
			ip6tc_flush_entries(chain, h->handle);
		}

		for (chain = ip6tc_first_chain(h->handle);
		     chain != NULL;
		     chain = ip6tc_next_chain(h->handle))
		{
			ip6tc_delete_chain(chain, h->handle);
		}
	}
	else
	{
		for (chain = iptc_first_chain(h->handle);
		     chain != NULL;
		     chain = iptc_next_chain(h->handle))
		{
			iptc_flush_entries(chain, h->handle);
		}

		for (chain = iptc_first_chain(h->handle);
		     chain != NULL;
		     chain = iptc_next_chain(h->handle))
		{
			iptc_delete_chain(chain, h->handle);
		}
	}
}

void
fw3_ipt_commit(struct fw3_ipt_handle *h)
{
	int rv;

	if (h->family == FW3_FAMILY_V6)
	{
		rv = ip6tc_commit(h->handle);
		if (!rv)
			fprintf(stderr, "ip6tc_commit(): %s\n", ip6tc_strerror(errno));
	}
	else
	{
		rv = iptc_commit(h->handle);
		if (!rv)
			fprintf(stderr, "iptc_commit(): %s\n", iptc_strerror(errno));
	}

	free(h);
}

struct fw3_ipt_rule *
fw3_ipt_rule_new(struct fw3_ipt_handle *h)
{
	struct fw3_ipt_rule *r;

	r = fw3_alloc(sizeof(*r));

	r->h = h;
	r->argv = fw3_alloc(sizeof(char *));
	r->argv[r->argc++] = "fw3";

	return r;
}


static bool
is_chain(struct fw3_ipt_handle *h, const char *name)
{
	if (h->family == FW3_FAMILY_V6)
		return ip6tc_is_chain(name, h->handle);
	else
		return iptc_is_chain(name, h->handle);
}

static char *
get_protoname(struct fw3_ipt_rule *r)
{
	const struct xtables_pprot *pp;

	if (r->protocol)
		for (pp = xtables_chain_protos; pp->name; pp++)
			if (pp->num == r->protocol)
				return (char *)pp->name;

	return NULL;
}

static struct xtables_match *
find_match(struct fw3_ipt_rule *r, const char *name)
{
	return xtables_find_match(name, XTF_TRY_LOAD, &r->matches);
}

static void
init_match(struct fw3_ipt_rule *r, struct xtables_match *m, bool no_clone)
{
	size_t s;
	struct xtables_globals *g;

	if (!m)
		return;

	s = XT_ALIGN(sizeof(struct xt_entry_match)) + m->size;

	m->m = fw3_alloc(s);
	strcpy(m->m->u.user.name, m->real_name ? m->real_name : m->name);
	m->m->u.user.revision = m->revision;
	m->m->u.match_size = s;

	/* free previous userspace data */
	if (m->udata_size)
	{
		free(m->udata);
		m->udata = fw3_alloc(m->udata_size);
	}

	if (m->init)
		m->init(m->m);

	/* don't merge options if no_clone is set and this match is a clone */
	if (no_clone && (m == m->next))
		return;

	/* merge option table */
	g = (r->h->family == FW3_FAMILY_V6) ? &xtg6 : &xtg;

	if (m->x6_options)
		g->opts = xtables_options_xfrm(g->orig_opts, g->opts,
									   m->x6_options, &m->option_offset);

	if (m->extra_opts)
		g->opts = xtables_merge_options(g->orig_opts, g->opts,
										m->extra_opts, &m->option_offset);
}

static bool
need_protomatch(struct fw3_ipt_rule *r, const char *pname)
{
	if (!pname)
		return false;

	if (!xtables_find_match(pname, XTF_DONT_LOAD, NULL))
		return true;

	return !r->protocol_loaded;
}

static struct xtables_match *
load_protomatch(struct fw3_ipt_rule *r)
{
	const char *pname = get_protoname(r);

	if (!need_protomatch(r, pname))
		return NULL;

	return find_match(r, pname);
}

static struct xtables_target *
get_target(struct fw3_ipt_rule *r, const char *name)
{
	size_t s;
	struct xtables_target *t;
	struct xtables_globals *g;

	bool chain = is_chain(r->h, name);

	if (chain)
		t = xtables_find_target(XT_STANDARD_TARGET, XTF_LOAD_MUST_SUCCEED);
	else
		t = xtables_find_target(name, XTF_TRY_LOAD);

	if (!t)
		return NULL;

	s = XT_ALIGN(sizeof(struct xt_entry_target)) + t->size;
	t->t = fw3_alloc(s);

	if (!t->real_name)
		strcpy(t->t->u.user.name, name);
	else
		strcpy(t->t->u.user.name, t->real_name);

	t->t->u.user.revision = t->revision;
	t->t->u.target_size = s;

	if (t->udata_size)
	{
		free(t->udata);
		t->udata = fw3_alloc(t->udata_size);
	}

	if (t->init)
		t->init(t->t);

	/* merge option table */
	g = (r->h->family == FW3_FAMILY_V6) ? &xtg6 : &xtg;

	if (t->x6_options)
		g->opts = xtables_options_xfrm(g->orig_opts, g->opts,
		                               t->x6_options, &t->option_offset);
	else
		g->opts = xtables_merge_options(g->orig_opts, g->opts,
		                                t->extra_opts, &t->option_offset);

	r->target = t;

	return t;
}

void
fw3_ipt_rule_proto(struct fw3_ipt_rule *r, struct fw3_protocol *proto)
{
	uint32_t pr;

	if (!proto || proto->any)
		return;

	pr = proto->protocol;

	if (r->h->family == FW3_FAMILY_V6)
	{
		if (pr == 1)
			pr = 58;

		r->e6.ipv6.proto = pr;
		r->e6.ipv6.flags |= IP6T_F_PROTO;

		if (proto->invert)
			r->e6.ipv6.invflags |= XT_INV_PROTO;
	}
	else
	{
		r->e.ip.proto = pr;

		if (proto->invert)
			r->e.ip.invflags |= XT_INV_PROTO;
	}

	r->protocol = pr;
}

void
fw3_ipt_rule_in_out(struct fw3_ipt_rule *r,
                    struct fw3_device *in, struct fw3_device *out)
{
	if (r->h->family == FW3_FAMILY_V6)
	{
		if (in && !in->any)
		{
			xtables_parse_interface(in->name, r->e6.ipv6.iniface,
			                                  r->e6.ipv6.iniface_mask);

			if (in->invert)
				r->e6.ipv6.invflags |= IP6T_INV_VIA_IN;
		}

		if (out && !out->any)
		{
			xtables_parse_interface(out->name, r->e6.ipv6.outiface,
			                                   r->e6.ipv6.outiface_mask);

			if (out->invert)
				r->e6.ipv6.invflags |= IP6T_INV_VIA_OUT;
		}
	}
	else
	{
		if (in && !in->any)
		{
			xtables_parse_interface(in->name, r->e.ip.iniface,
			                                  r->e.ip.iniface_mask);

			if (in->invert)
				r->e.ip.invflags |= IPT_INV_VIA_IN;
		}

		if (out && !out->any)
		{
			xtables_parse_interface(out->name, r->e.ip.outiface,
			                                   r->e.ip.outiface_mask);

			if (out->invert)
				r->e.ip.invflags |= IPT_INV_VIA_OUT;
		}
	}
}


static void
ip4prefix2mask(int prefix, struct in_addr *mask)
{
	mask->s_addr = htonl(~((1 << (32 - prefix)) - 1));
}

static void
ip6prefix2mask(int prefix, struct in6_addr *mask)
{
	char *p = (char *)mask;

	if (prefix > 0)
	{
		memset(p, 0xff, prefix / 8);
		memset(p + (prefix / 8) + 1, 0, (128 - prefix) / 8);
		p[prefix / 8] = 0xff << (8 - (prefix & 7));
	}
	else
	{
		memset(mask, 0, sizeof(*mask));
	}
}

void
fw3_ipt_rule_src_dest(struct fw3_ipt_rule *r,
                      struct fw3_address *src, struct fw3_address *dest)
{
	int i;

	if ((src && src->range) || (dest && dest->range))
	{
		fw3_ipt_rule_addarg(r, false, "-m", "iprange");
	}

	if (src && src->set)
	{
		if (src->range)
		{
			fw3_ipt_rule_addarg(r, src->invert, "--src-range",
			                    fw3_address_to_string(src, false));
		}
		else if (r->h->family == FW3_FAMILY_V6)
		{
			r->e6.ipv6.src = src->address.v6;
			ip6prefix2mask(src->mask, &r->e6.ipv6.smsk);

			for (i = 0; i < 4; i++)
				r->e6.ipv6.src.s6_addr32[i] &= r->e6.ipv6.smsk.s6_addr32[i];

			if (src->invert)
				r->e6.ipv6.invflags |= IP6T_INV_SRCIP;
		}
		else
		{
			r->e.ip.src = src->address.v4;
			ip4prefix2mask(src->mask, &r->e.ip.smsk);

			r->e.ip.src.s_addr &= r->e.ip.smsk.s_addr;

			if (src->invert)
				r->e.ip.invflags |= IPT_INV_SRCIP;
		}
	}

	if (dest && dest->set)
	{
		if (dest->range)
		{
			fw3_ipt_rule_addarg(r, dest->invert, "--dst-range",
			                    fw3_address_to_string(dest, false));
		}
		else if (r->h->family == FW3_FAMILY_V6)
		{
			r->e6.ipv6.dst = dest->address.v6;
			ip6prefix2mask(dest->mask, &r->e6.ipv6.dmsk);

			for (i = 0; i < 4; i++)
				r->e6.ipv6.dst.s6_addr32[i] &= r->e6.ipv6.dmsk.s6_addr32[i];

			if (dest->invert)
				r->e6.ipv6.invflags |= IP6T_INV_DSTIP;
		}
		else
		{
			r->e.ip.dst = dest->address.v4;
			ip4prefix2mask(dest->mask, &r->e.ip.dmsk);

			r->e.ip.dst.s_addr &= r->e.ip.dmsk.s_addr;

			if (dest->invert)
				r->e.ip.invflags |= IPT_INV_DSTIP;
		}
	}
}

void
fw3_ipt_rule_sport_dport(struct fw3_ipt_rule *r,
                         struct fw3_port *sp, struct fw3_port *dp)
{
	char buf[sizeof("65535:65535\0")];

	if ((!sp || !sp->set) && (!dp || !dp->set))
		return;

	if (!get_protoname(r))
		return;

	if (sp && sp->set)
	{
		if (sp->port_min == sp->port_max)
			sprintf(buf, "%u", sp->port_min);
		else
			sprintf(buf, "%u:%u", sp->port_min, sp->port_max);

		fw3_ipt_rule_addarg(r, sp->invert, "--sport", buf);
	}

	if (dp && dp->set)
	{
		if (dp->port_min == dp->port_max)
			sprintf(buf, "%u", dp->port_min);
		else
			sprintf(buf, "%u:%u", dp->port_min, dp->port_max);

		fw3_ipt_rule_addarg(r, dp->invert, "--dport", buf);
	}
}

void
fw3_ipt_rule_mac(struct fw3_ipt_rule *r, struct fw3_mac *mac)
{
	if (!mac)
		return;

	fw3_ipt_rule_addarg(r, false, "-m", "mac");
	fw3_ipt_rule_addarg(r, mac->invert, "--mac-source", ether_ntoa(&mac->mac));
}

void
fw3_ipt_rule_icmptype(struct fw3_ipt_rule *r, struct fw3_icmptype *icmp)
{
	char buf[sizeof("255/255\0")];

	if (!icmp)
		return;

	if (r->h->family == FW3_FAMILY_V6)
	{
		if (icmp->code6_min == 0 && icmp->code6_max == 0xFF)
			sprintf(buf, "%u", icmp->type6);
		else
			sprintf(buf, "%u/%u", icmp->type6, icmp->code6_min);

		fw3_ipt_rule_addarg(r, icmp->invert, "--icmpv6-type", buf);
	}
	else
	{
		if (icmp->code_min == 0 && icmp->code_max == 0xFF)
			sprintf(buf, "%u", icmp->type);
		else
			sprintf(buf, "%u/%u", icmp->type, icmp->code_min);

		fw3_ipt_rule_addarg(r, icmp->invert, "--icmp-type", buf);
	}
}

void
fw3_ipt_rule_limit(struct fw3_ipt_rule *r, struct fw3_limit *limit)
{
	char buf[sizeof("-4294967296/second\0")];

	if (!limit || limit->rate <= 0)
		return;

	fw3_ipt_rule_addarg(r, false, "-m", "limit");

	sprintf(buf, "%u/%s", limit->rate, fw3_limit_units[limit->unit]);
	fw3_ipt_rule_addarg(r, limit->invert, "--limit", buf);

	if (limit->burst > 0)
	{
		sprintf(buf, "%u", limit->burst);
		fw3_ipt_rule_addarg(r, limit->invert, "--limit-burst", buf);
	}
}

void
fw3_ipt_rule_ipset(struct fw3_ipt_rule *r, struct fw3_ipset *ipset,
                   bool invert)
{
	char buf[sizeof("dst,dst,dst\0")];
	char *p = buf;

	struct fw3_ipset_datatype *type;

	if (!ipset)
		return;

	list_for_each_entry(type, &ipset->datatypes, list)
	{
		if (p > buf)
			*p++ = ',';

		p += sprintf(p, "%s", type->dest ? "dst" : "src");
	}

	fw3_ipt_rule_addarg(r, false, "-m", "set");

	fw3_ipt_rule_addarg(r, invert, "--match-set",
	                    ipset->external ? ipset->external : ipset->name);

	fw3_ipt_rule_addarg(r, false, buf, NULL);
}

void
fw3_ipt_rule_time(struct fw3_ipt_rule *r, struct fw3_time *time)
{
	int i;
	struct tm empty = { 0 };

	char buf[84]; /* sizeof("1,2,3,...,30,31\0") */
	char *p;

	bool d1 = memcmp(&time->datestart, &empty, sizeof(empty));
	bool d2 = memcmp(&time->datestop, &empty, sizeof(empty));

	if (!d1 && !d2 && !time->timestart && !time->timestop &&
	    !(time->monthdays & 0xFFFFFFFE) && !(time->weekdays & 0xFE))
	{
		return;
	}

	fw3_ipt_rule_addarg(r, false, "-m", "time");

	if (time->utc)
		fw3_ipt_rule_addarg(r, false, "--utc", NULL);

	if (d1)
	{
		strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &time->datestart);
		fw3_ipt_rule_addarg(r, false, "--datestart", buf);
	}

	if (d2)
	{
		strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &time->datestop);
		fw3_ipt_rule_addarg(r, false, "--datestop", buf);
	}

	if (time->timestart)
	{
		sprintf(buf, "%02d:%02d:%02d",
		        time->timestart / 3600,
		        time->timestart % 3600 / 60,
		        time->timestart % 60);

		fw3_ipt_rule_addarg(r, false, "--timestart", buf);
	}

	if (time->timestop)
	{
		sprintf(buf, "%02d:%02d:%02d",
		        time->timestop / 3600,
		        time->timestop % 3600 / 60,
		        time->timestop % 60);

		fw3_ipt_rule_addarg(r, false, "--timestop", buf);
	}

	if (time->monthdays & 0xFFFFFFFE)
	{
		for (i = 1, p = buf; i < 32; i++)
		{
			if (hasbit(time->monthdays, i))
			{
				if (p > buf)
					*p++ = ',';

				p += sprintf(p, "%u", i);
			}
		}

		fw3_ipt_rule_addarg(r, hasbit(time->monthdays, 0), "--monthdays", buf);
	}

	if (time->weekdays & 0xFE)
	{
		for (i = 1, p = buf; i < 8; i++)
		{
			if (hasbit(time->weekdays, i))
			{
				if (p > buf)
					*p++ = ',';

				p += sprintf(p, "%u", i);
			}
		}

		fw3_ipt_rule_addarg(r, hasbit(time->weekdays, 0), "--weekdays", buf);
	}
}

void
fw3_ipt_rule_mark(struct fw3_ipt_rule *r, struct fw3_mark *mark)
{
	char buf[sizeof("0xFFFFFFFF/0xFFFFFFFF\0")];

	if (!mark || !mark->set)
		return;

	if (mark->mask < 0xFFFFFFFF)
		sprintf(buf, "0x%x/0x%x", mark->mark, mark->mask);
	else
		sprintf(buf, "0x%x", mark->mark);

	fw3_ipt_rule_addarg(r, false, "-m", "mark");
	fw3_ipt_rule_addarg(r, mark->invert, "--mark", buf);
}

void
fw3_ipt_rule_comment(struct fw3_ipt_rule *r, const char *fmt, ...)
{
	va_list ap;
	char buf[256];

	if (!fmt || !*fmt)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);

	fw3_ipt_rule_addarg(r, false, "-m", "comment");
	fw3_ipt_rule_addarg(r, false, "--comment", buf);
}

void
fw3_ipt_rule_extra(struct fw3_ipt_rule *r, const char *extra)
{
	char *p, **tmp, *s;

	if (!extra || !*extra)
		return;

	s = fw3_strdup(extra);

	for (p = strtok(s, " \t"); p; p = strtok(NULL, " \t"))
	{
		tmp = realloc(r->argv, (r->argc + 1) * sizeof(*r->argv));

		if (!tmp)
			break;

		r->argv = tmp;
		r->argv[r->argc++] = fw3_strdup(p);
	}

	free(s);
}

static void
rule_print6(struct ip6t_entry *e)
{
	char buf[INET6_ADDRSTRLEN];
	char *pname;

	if (e->ipv6.flags & IP6T_F_PROTO)
	{
		if (e->ipv6.flags & XT_INV_PROTO)
			printf(" !");

		pname = get_protoname(container_of(e, struct fw3_ipt_rule, e6));

		if (pname)
			printf(" -p %s", pname);
		else
			printf(" -p %u", e->ipv6.proto);
	}

	if (e->ipv6.iniface[0])
	{
		if (e->ipv6.flags & IP6T_INV_VIA_IN)
			printf(" !");

		printf(" -i %s", e->ipv6.iniface);
	}

	if (e->ipv6.outiface[0])
	{
		if (e->ipv6.flags & IP6T_INV_VIA_OUT)
			printf(" !");

		printf(" -o %s", e->ipv6.outiface);
	}

	if (memcmp(&e->ipv6.src, &in6addr_any, sizeof(struct in6_addr)))
	{
		if (e->ipv6.flags & IP6T_INV_SRCIP)
			printf(" !");

		printf(" -s %s/%u", inet_ntop(AF_INET6, &e->ipv6.src, buf, sizeof(buf)),
		                    xtables_ip6mask_to_cidr(&e->ipv6.smsk));
	}

	if (memcmp(&e->ipv6.dst, &in6addr_any, sizeof(struct in6_addr)))
	{
		if (e->ipv6.flags & IP6T_INV_DSTIP)
			printf(" !");

		printf(" -d %s/%u", inet_ntop(AF_INET6, &e->ipv6.dst, buf, sizeof(buf)),
		                    xtables_ip6mask_to_cidr(&e->ipv6.dmsk));
	}
}

static void
rule_print4(struct ipt_entry *e)
{
	struct in_addr in_zero = { 0 };
	char buf[sizeof("255.255.255.255\0")];
	char *pname;

	if (e->ip.proto)
	{
		if (e->ip.flags & XT_INV_PROTO)
			printf(" !");

		pname = get_protoname(container_of(e, struct fw3_ipt_rule, e));

		if (pname)
			printf(" -p %s", pname);
		else
			printf(" -p %u", e->ip.proto);
	}

	if (e->ip.iniface[0])
	{
		if (e->ip.flags & IPT_INV_VIA_IN)
			printf(" !");

		printf(" -i %s", e->ip.iniface);
	}

	if (e->ip.outiface[0])
	{
		if (e->ip.flags & IPT_INV_VIA_OUT)
			printf(" !");

		printf(" -o %s", e->ip.outiface);
	}

	if (memcmp(&e->ip.src, &in_zero, sizeof(struct in_addr)))
	{
		if (e->ip.flags & IPT_INV_SRCIP)
			printf(" !");

		printf(" -s %s/%u", inet_ntop(AF_INET, &e->ip.src, buf, sizeof(buf)),
		                    xtables_ipmask_to_cidr(&e->ip.smsk));
	}

	if (memcmp(&e->ip.dst, &in_zero, sizeof(struct in_addr)))
	{
		if (e->ip.flags & IPT_INV_DSTIP)
			printf(" !");

		printf(" -d %s/%u", inet_ntop(AF_INET, &e->ip.dst, buf, sizeof(buf)),
		                    xtables_ipmask_to_cidr(&e->ip.dmsk));
	}
}

static void
rule_print(struct fw3_ipt_rule *r, const char *chain)
{
	struct xtables_rule_match *rm;
	struct xtables_match *m;
	struct xtables_target *t;

	printf("-A %s", chain);

	if (r->h->family == FW3_FAMILY_V6)
		rule_print6(&r->e6);
	else
		rule_print4(&r->e);

	for (rm = r->matches; rm; rm = rm->next)
	{
		m = rm->match;
		printf(" -m %s", m->alias ? m->alias(m->m) : m->m->u.user.name);

		if (m->save)
			m->save(&r->e.ip, m->m);
	}

	if (r->target)
	{
		t = r->target;
		printf(" -j %s", t->alias ? t->alias(t->t) : t->t->u.user.name);

		if (t->save)
			t->save(&r->e.ip, t->t);
	}

	printf("\n");
}

static bool
parse_option(struct fw3_ipt_rule *r, int optc, bool inv)
{
	struct xtables_rule_match *m;
	struct xtables_match *em;

	/* is a target option */
	if (r->target && (r->target->parse || r->target->x6_parse) &&
		optc >= r->target->option_offset &&
		optc < (r->target->option_offset + 256))
	{
		xtables_option_tpcall(optc, r->argv, inv, r->target, &r->e);
		return false;
	}

	/* try to dispatch argument to one of the match parsers */
	for (m = r->matches; m; m = m->next)
	{
		em = m->match;

		if (m->completed || (!em->parse && !em->x6_parse))
			continue;

		if (optc < em->option_offset ||
			optc >= (em->option_offset + 256))
			continue;

		xtables_option_mpcall(optc, r->argv, inv, em, &r->e);
		return false;
	}

	/* unhandled option, might belong to a protocol match */
	if ((em = load_protomatch(r)) != NULL)
	{
		init_match(r, em, false);

		r->protocol_loaded = true;
		optind--;

		return true;
	}

	if (optc == ':')
		fprintf(stderr, "parse_option(): option '%s' needs argument\n",
		        r->argv[optind-1]);

	if (optc == '?')
		fprintf(stderr, "parse_option(): unknown option '%s'\n",
		        r->argv[optind-1]);

	return false;
}

void
fw3_ipt_rule_addarg(struct fw3_ipt_rule *r, bool inv,
                    const char *k, const char *v)
{
	int n;
	char **tmp;

	if (!k)
		return;

	n = inv + !!k + !!v;
	tmp = realloc(r->argv, (r->argc + n) * sizeof(*tmp));

	if (!tmp)
		return;

	r->argv = tmp;

	if (inv)
		r->argv[r->argc++] = fw3_strdup("!");

	r->argv[r->argc++] = fw3_strdup(k);

	if (v)
		r->argv[r->argc++] = fw3_strdup(v);
}

void
fw3_ipt_rule_append(struct fw3_ipt_rule *r, const char *fmt, ...)
{
	size_t s;
	struct xtables_rule_match *m;
	struct xtables_match *em;
	struct xtables_target *et;
	struct xtables_globals *g;
	struct ipt_entry *e;
	struct ip6t_entry *e6;

	int i, optc;
	bool inv = false;
	char buf[32];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);

	g = (r->h->family == FW3_FAMILY_V6) ? &xtg6 : &xtg;
	g->opts = g->orig_opts;

	optind = 0;
	opterr = 0;

	while ((optc = getopt_long(r->argc, r->argv, "m:j:", g->opts, NULL)) != -1)
	{
		switch (optc)
		{
		case 'm':
			em = find_match(r, optarg);

			if (!em)
			{
				fprintf(stderr, "fw3_ipt_rule_append(): Can't find match '%s'\n", optarg);
				return;
			}

			init_match(r, em, true);
			break;

		case 'j':
			et = get_target(r, optarg);

			if (!et)
			{
				fprintf(stderr, "fw3_ipt_rule_append(): Can't find target '%s'\n", optarg);
				return;
			}

			break;

		case 1:
			if ((optarg[0] == '!') && (optarg[1] == '\0'))
			{
				inv = true;
				continue;
			}

			fprintf(stderr, "fw3_ipt_rule_append(): Bad argument '%s'\n", optarg);
			return;

		default:
			if (parse_option(r, optc, inv))
				continue;
			break;
		}

		inv = false;
	}

	for (m = r->matches; m; m = m->next)
		xtables_option_mfcall(m->match);

	if (r->target)
		xtables_option_tfcall(r->target);

	if (fw3_pr_debug)
		rule_print(r, buf);

	if (r->h->family == FW3_FAMILY_V6)
	{
		s = XT_ALIGN(sizeof(struct ip6t_entry));

		for (m = r->matches; m; m = m->next)
			s += m->match->m->u.match_size;

		e6 = fw3_alloc(s + r->target->t->u.target_size);

		memcpy(e6, &r->e6, sizeof(struct ip6t_entry));

		e6->target_offset = s;
		e6->next_offset = s + r->target->t->u.target_size;

		s = 0;

		for (m = r->matches; m; m = m->next)
		{
			memcpy(e6->elems + s, m->match->m, m->match->m->u.match_size);
			s += m->match->m->u.match_size;
		}

		memcpy(e6->elems + s, r->target->t, r->target->t->u.target_size);
		ip6tc_append_entry(buf, e6, r->h->handle);
		free(e6);
	}
	else
	{
		s = XT_ALIGN(sizeof(struct ipt_entry));

		for (m = r->matches; m; m = m->next)
			s += m->match->m->u.match_size;

		e = fw3_alloc(s + r->target->t->u.target_size);

		memcpy(e, &r->e, sizeof(struct ipt_entry));

		e->target_offset = s;
		e->next_offset = s + r->target->t->u.target_size;

		s = 0;

		for (m = r->matches; m; m = m->next)
		{
			memcpy(e->elems + s, m->match->m, m->match->m->u.match_size);
			s += m->match->m->u.match_size;
		}

		memcpy(e->elems + s, r->target->t, r->target->t->u.target_size);

		if (!iptc_append_entry(buf, e, r->h->handle))
			fprintf(stderr, "iptc_append_entry(): %s\n", iptc_strerror(errno));

		free(e);
	}

	for (i = 1; i < r->argc; i++)
		free(r->argv[i]);

	free(r->argv);

	xtables_rule_matches_free(&r->matches);

	free(r->target->t);
	free(r);

	/* reset all targets and matches */
	for (em = xtables_matches; em; em = em->next)
		em->mflags = 0;

	for (et = xtables_targets; et; et = et->next)
	{
		et->tflags = 0;
		et->used = 0;
	}

	xtables_free_opts(1);
}

struct fw3_ipt_rule *
fw3_ipt_rule_create(struct fw3_ipt_handle *handle, struct fw3_protocol *proto,
                    struct fw3_device *in, struct fw3_device *out,
                    struct fw3_address *src, struct fw3_address *dest)
{
	struct fw3_ipt_rule *r;

	r = fw3_ipt_rule_new(handle);

	fw3_ipt_rule_proto(r, proto);
	fw3_ipt_rule_in_out(r, in, out);
	fw3_ipt_rule_src_dest(r, src, dest);

	return r;
}