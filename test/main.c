/* Read kernel IPv4 routes from main routing table
 *
 * Copyright (c) 2021  Joachim Wiberg <troglobit@gmail.com>
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

#include <err.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUFSIZE 8192
#define NLMSG_TAIL(nmsg) \
	((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

struct route_info {
	struct in_addr dst;
	unsigned short len;
	struct in_addr src;
	struct in_addr gw;

	int ifindex;
	char ifname[IFNAMSIZ];
};

static int opt_n = 0;


static int read_netlink(int sd, char *buf, size_t len, unsigned int seq, unsigned int pid)
{
	struct nlmsghdr *nlh;
	int msg_len = 0;
	int sz = 0;

	do {
		if ((sz = recv(sd, buf, len - msg_len, 0)) < 0)
			return -1;

		nlh = (struct nlmsghdr *)buf;
		if ((NLMSG_OK(nlh, sz) == 0) || (nlh->nlmsg_type == NLMSG_ERROR))
			return -1;

		if (nlh->nlmsg_type == NLMSG_DONE)
			break;

		buf     += sz;
		msg_len += sz;

		if ((nlh->nlmsg_flags & NLM_F_MULTI) == 0)
			break;
	}
	while ((nlh->nlmsg_seq != seq) || (nlh->nlmsg_pid != pid));

	return msg_len;
}

static void print_route(struct route_info *ri)
{
	char tmp[INET_ADDRSTRLEN + 5];
	struct in_addr nil = { 0 };

	if (opt_n || memcmp(&ri->dst, &nil, sizeof(nil))) {
		char prefix[5];

		strcpy(tmp, inet_ntoa(ri->dst));
		snprintf(prefix, sizeof(prefix), "/%d", ri->len);
		strcat(tmp, prefix);
	} else
		sprintf(tmp, "default");
	fprintf(stdout, "%-20s ", tmp);

	fprintf(stdout, "%-16s ", inet_ntoa(ri->gw));
	fprintf(stdout, "%-16s ", ri->ifname);
	fprintf(stdout, "%-16s\n", inet_ntoa(ri->src));
}

static int parse_nlmsg(struct nlmsghdr *nlh, struct route_info *ri)
{
	struct rtattr *a;
	struct rtmsg *r;
	int len;

	r = (struct rtmsg *)NLMSG_DATA(nlh);
	if ((r->rtm_family != AF_INET) || (r->rtm_table != RT_TABLE_MAIN))
		return -1;

	len = RTM_PAYLOAD(nlh);
	for (a = RTM_RTA(r); RTA_OK(a, len); a = RTA_NEXT(a, len)) {
		switch (a->rta_type) {
		case RTA_OIF:
			ri->ifindex = *(int *)RTA_DATA(a);
			if_indextoname(ri->ifindex, ri->ifname);
			break;

		case RTA_GATEWAY:
			memcpy(&ri->gw, RTA_DATA(a), sizeof(ri->gw));
			break;

		case RTA_PREFSRC:
			memcpy(&ri->src, RTA_DATA(a), sizeof(ri->src));
			break;

		case RTA_DST:
			memcpy(&ri->dst, RTA_DATA(a), sizeof(ri->dst));
			ri->len = r->rtm_dst_len;
			break;
		}
	}

	print_route(ri);

	return 0;
}

int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data,
	      int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
		fprintf(stderr,
			"addattr_l ERROR: message exceeded bound of %d\n",
			maxlen);
		return -1;
	}
	rta = NLMSG_TAIL(n);
	rta->rta_type = type;
	rta->rta_len = len;
	if (alen)
		memcpy(RTA_DATA(rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
	return 0;
}

int main(int argc, char *argv[])
{
	static char buf[BUFSIZE] = { 0 };
	struct nlmsghdr *nlmsg;
    struct {
		struct nlmsghdr	n;
		struct rtmsg	r;
		char			array[1024];
	} req = {
		.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)),
		.n.nlmsg_flags = NLM_F_REQUEST,
		.n.nlmsg_type = RTM_GETROUTE,
		.r.rtm_family = AF_INET,
	};
	struct route_info ri;
	int sd, len;
	int seq = 0;

	if (argc > 1 && !strcmp(argv[1], "-n"))
		opt_n = 1;

	if ((sd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) < 0)
		err(1, "netlink socket");

	nlmsg = (struct nlmsghdr *)buf;

	req.n.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
	req.n.nlmsg_seq = seq++;
	req.n.nlmsg_pid = getpid();

    // req.r.rtm_family = AF_INET; // AF_INET
	// req.r.rtm_flags |= RTM_F_LOOKUP_TABLE;

    union {
        uint8_t arr[4];
        uint32_t u32;
    }
        ip_addr = {
        .arr = {10,0,0,2}
    };

    addattr_l(&req.n, sizeof(req),
                RTA_DST, &ip_addr, 4);
	req.r.rtm_dst_len = 4;

	printf("n.nlmsg_len %d\n", req.n.nlmsg_len);

	if (send(sd, &req.n, req.n.nlmsg_len, 0) < 0)
		err(1, "Failed netlink route request");

	len = read_netlink(sd, buf, sizeof(buf), seq, getpid());
	if (len < 0)
		err(1, "Failed reading netlink response");

	fprintf(stdout, "\e[7m%-20s %-16s %-16s %-16s\e[0m\n", "Destination", "Gateway", "Interface", "Source");
	for (; NLMSG_OK(nlmsg, len); nlmsg = NLMSG_NEXT(nlmsg, len)) {
		memset(&ri, 0, sizeof(ri));
		parse_nlmsg(nlmsg, &ri);
	}
	close(sd);

	return 0;
}
