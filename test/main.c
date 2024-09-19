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


#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "main.h"

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

int msg_add_addr(struct nlmsghdr *n, int maxlen, int type, const void *data,
	      int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
		printf("addattr_l ERROR: message exceeded bound of %d\n", maxlen);
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

int get_route_by_addr(uint8_t *addr, struct route_info *route)
{
  int sock, len;
  static char buf[BUFSIZE] = { 0 };
  struct nlmsghdr *nlmsg = (struct nlmsghdr *)buf;

  if ((sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) < 0)
		err(0, "netlink socket");

  struct {
		struct nlmsghdr	n;
		struct rtmsg	  r;
		char			      array[1024];
	} req = {
		.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)),
		.n.nlmsg_flags = NLM_F_REQUEST,
		.n.nlmsg_type = RTM_GETROUTE,
		.r.rtm_family = AF_INET,
    .r.rtm_dst_len = sizeof(addr) * 4, //bytelen should be 32
    .array = {0}
	};

  msg_add_addr(&req.n, sizeof(req),
              RTA_DST, addr, sizeof(addr)/2); //byteseq should be 4

  if (send(sock, &req.n, req.n.nlmsg_len, 0) < 0)
		err(0, "Failed netlink route request");

	len = read_netlink(sock, buf, sizeof(buf), 0, getpid());
	if (len < 0)
		err(0, "Failed reading netlink response");

	for (; NLMSG_OK(nlmsg, len); nlmsg = NLMSG_NEXT(nlmsg, len)) {
		parse_nlmsg(nlmsg, route);
    return 1;
	}

	close(sock);

	return 0;
}

int main()
{
  union {
      uint8_t arr[4];
      uint32_t u32;
  }
      ip_addr = {
      .arr = {10,0,0,5}
  };
  struct route_info find;
  get_route_by_addr(ip_addr.arr, &find);

}
