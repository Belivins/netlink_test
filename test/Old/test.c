#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netlink/netlink.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <linux/in_route.h>
#include <linux/icmpv6.h>
#include <errno.h>
#include <stdbool.h>


// #include <iproute/ip_common.h>
// #include <iproute/utils.h>
// #include "utils.h"
// #include "ip_common.h"
// #include "nh_common.h"

typedef struct
{
	__u16 flags;
	__u16 bytelen;
	__s16 bitlen;
	/* These next two fields match rtvia */
	__u16 family;
	__u32 data[64];
} inet_prefix;

static struct
{
	unsigned int tb;
	int cloned;
	int flushed;
	char *flushb;
	int flushp;
	int flushe;
	int protocol, protocolmask;
	int scope, scopemask;
	__u64 typemask;
	int tos, tosmask;
	int iif, iifmask;
	int oif, oifmask;
	int mark, markmask;
	int realm, realmmask;
	__u32 metric, metricmask;
	inet_prefix rprefsrc;
	inet_prefix rvia;
	inet_prefix rdst;
	inet_prefix mdst;
	inet_prefix rsrc;
	inet_prefix msrc;
} filter;

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

static int iproute_get(int argc, char **argv)
{
	struct {
		struct nlmsghdr	n;
		struct rtmsg		r;
		char			buf[1024];
	} req = {
		.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)),
		.n.nlmsg_flags = NLM_F_REQUEST,
		.n.nlmsg_type = RTM_GETROUTE,
		.r.rtm_family = AF_UNSPEC,
	};
	char  *idev = NULL;
	char  *odev = NULL;
	struct nlmsghdr *answer;
	int connected = 0;
	int fib_match = 0;
	int from_ok = 0;
	unsigned int mark = 0;
	bool address_found = false;

	iproute_reset_filter(0);
	filter.cloned = 2;

	while (argc > 0) {
		
        inet_prefix addr;

        if (strcmp(*argv, "to") == 0) {
            NEXT_ARG();
        }
        get_prefix(&addr, *argv, req.r.rtm_family);
        if (req.r.rtm_family == AF_UNSPEC)
            req.r.rtm_family = addr.family;
        if (addr.bytelen)
            addattr_l(&req.n, sizeof(req),
                    RTA_DST, &addr.data, addr.bytelen);
        if (req.r.rtm_family == AF_INET && addr.bitlen != 32) {
            fprintf(stderr,
                "Warning: /%u as prefix is invalid, only /32 (or none) is supported.\n",
                addr.bitlen);
            req.r.rtm_dst_len = 32;
        } else if (req.r.rtm_family == AF_INET6 && addr.bitlen != 128) {
            fprintf(stderr,
                "Warning: /%u as prefix is invalid, only /128 (or none) is supported.\n",
                addr.bitlen);
            req.r.rtm_dst_len = 128;
        } else
            req.r.rtm_dst_len = addr.bitlen;
        address_found = true;
		
		argc--; argv++;
	}

	if (!address_found) {
		fprintf(stderr, "need at least a destination address\n");
		return -1;
	}

	if (idev || odev)  {
		int idx;

		if (idev) {
			idx = ll_name_to_index(idev);
			if (!idx)
				return nodev(idev);
			addattr32(&req.n, sizeof(req), RTA_IIF, idx);
		}
		if (odev) {
			idx = ll_name_to_index(odev);
			if (!idx)
				return nodev(odev);
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}
	if (mark)
		addattr32(&req.n, sizeof(req), RTA_MARK, mark);

	if (req.r.rtm_family == AF_UNSPEC)
		req.r.rtm_family = AF_INET;

	/* Only IPv4 supports the RTM_F_LOOKUP_TABLE flag */
	if (req.r.rtm_family == AF_INET)
		req.r.rtm_flags |= RTM_F_LOOKUP_TABLE;
	if (fib_match)
		req.r.rtm_flags |= RTM_F_FIB_MATCH;

	if (connected && !from_ok) {
		struct rtmsg *r = NLMSG_DATA(answer);
		int len = answer->nlmsg_len;
		struct rtattr *tb[RTA_MAX+1];

		if (print_route(answer, (void *)stdout) < 0) {
			fprintf(stderr, "An error :-)\n");
			free(answer);
			return -1;
		}

		if (answer->nlmsg_type != RTM_NEWROUTE) {
			fprintf(stderr, "Not a route?\n");
			free(answer);
			return -1;
		}
		len -= NLMSG_LENGTH(sizeof(*r));
		if (len < 0) {
			fprintf(stderr, "Wrong len %d\n", len);
			free(answer);
			return -1;
		}

		parse_rtattr(tb, RTA_MAX, RTM_RTA(r), len);

		if (tb[RTA_PREFSRC]) {
			tb[RTA_PREFSRC]->rta_type = RTA_SRC;
			r->rtm_src_len = 8*RTA_PAYLOAD(tb[RTA_PREFSRC]);
		} else if (!tb[RTA_SRC]) {
			fprintf(stderr, "Failed to connect the route\n");
			free(answer);
			return -1;
		}
		if (!odev && tb[RTA_OIF])
			tb[RTA_OIF]->rta_type = 0;
		if (tb[RTA_GATEWAY])
			tb[RTA_GATEWAY]->rta_type = 0;
		if (tb[RTA_VIA])
			tb[RTA_VIA]->rta_type = 0;
		if (!idev && tb[RTA_IIF])
			tb[RTA_IIF]->rta_type = 0;
		req.n.nlmsg_flags = NLM_F_REQUEST;
		req.n.nlmsg_type = RTM_GETROUTE;

		free(answer);
	}

	if (print_route(answer, (void *)stdout) < 0) {
		fprintf(stderr, "An error :-)\n");
		free(answer);
		return -1;
	}

	free(answer);
	return 0;
}

void main()
{
    char *srt = "10.0.0.1";
    iproute_get(1, srt);
}