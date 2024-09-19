#include <stdio.h>
#include <signal.h>
#include <netlink/socket.h>
#include <netlink/cache.h>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/link/ipgre.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/route.h>
#include <netlink/cli/utils.h>

#define IPv4_FMT "%hhu.%hhu.%hhu.%hhu"
#define IPv6_FMT "%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX:%02hhX%02hhX"
#define IPv4_ARG(p) p[0], p[1], p[2], p[3]
#define IPv6_ARG(p) p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]

static int quit = 0;
struct nl_cache_mngr *mngr;
struct nl_cache *lc, *nc, *ac, *rc;
struct nl_sock *sock;

static struct nl_dump_params dp = {
	.dp_type = NL_DUMP_LINE,
};

static void sigint(int arg)
{
	quit = 1;
}

int is_tun(char *name)
{
  if(strstr(name, "gre"))
    return 1;
  return 0;
}

int is_gre(char *name)
{
  printf("is_gre: %s\n", name);
  return !strcmp(name, "gre");
}

struct ip {
  union {
    uint32_t u32;
    uint8_t  arr[4];
  };
};

struct gre {
  char *name;
  char *type;
  struct ip local;
  struct ip remote;
};

// Отслеживание поднятия тунеля
static void link_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{

  struct rtnl_link *link = (struct rtnl_link *) obj;

  if(link && is_gre(rtnl_link_get_type(link))){
    struct gre tunnnel = {
      .type       = rtnl_link_get_type(link),
      .name       = rtnl_link_get_name (link),
      .local.u32  = rtnl_link_ipgre_get_local(link),
      .remote.u32 = rtnl_link_ipgre_get_remote(link),
    };

    printf("%s: %s type %s local "IPv4_FMT" remote "IPv4_FMT"\n",
      __func__, tunnnel.name, tunnnel.type, IPv4_ARG(tunnnel.local.arr), IPv4_ARG(tunnnel.remote.arr));
  }

	nl_object_dump(obj, &dp);
}

static void neigh_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
  // printf("%s\n", __func__);
	// nl_object_dump(obj, &dp);
}

// Отслеживание установки ip адреса тунеля
static void addr_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
  // printf("%s\n", __func__);
  struct rtnl_addr *addr = (struct rtnl_addr *) obj;
  struct rtnl_link *link = rtnl_addr_get_link(addr);

  if(link && is_tun(rtnl_link_get_name (link))){
    if(addr)
      switch (rtnl_addr_get_family(addr))
      {
        case AF_INET: {
            struct nl_addr *ipaddr = rtnl_addr_get_local(addr);
            uint8_t ip[4];
            memcpy (ip, nl_addr_get_binary_addr(ipaddr), sizeof(ip));
            printf("%s addr "IPv4_FMT"\n", rtnl_link_get_name(link), IPv4_ARG(ip));
          }
          break;
        // Не интересно
        // case AF_INET6: {
        //     uint8_t ipv6[16];
        //     memcpy (ipv6, nl_addr_get_binary_addr(ipaddr), sizeof(ipv6));
        //     printf("%s addrv6 " IPv6_FMT"\n", rtnl_link_get_name(link), IPv6_ARG(ipv6));
        //   }
        //   break;

        default:
          break;
      }
  }

	// nl_object_dump(obj, &dp);
}

// Отслеживание маршрутов в туннель
static void route_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
  // printf("%s\n", __func__);
  struct rtnl_route *route = (struct rtnl_route *) obj;
  int count_nhop = rtnl_route_get_nnexthops(route);

  for(int i = 0; i <= count_nhop; i++) {
    struct rtnl_nexthop *nhop = rtnl_route_nexthop_n(route, i);

    if (nhop) {
      struct rtnl_link *link = rtnl_link_get(lc, rtnl_route_nh_get_ifindex(nhop));
      if(link && is_tun(rtnl_link_get_name(link)))
      {
        struct nl_addr *ipaddr = rtnl_route_get_dst(route);
        uint8_t dst_len = nl_addr_get_prefixlen(ipaddr);
        if (dst_len != 0){
          switch (rtnl_route_get_family(route))
          {
            case AF_INET: {
                uint8_t ip[4];
                memcpy (ip, nl_addr_get_binary_addr(ipaddr), sizeof(ip));
                printf("%s route "IPv4_FMT"/%d\n", rtnl_link_get_name(link), IPv4_ARG(ip),dst_len);
              }
              break;
            // Не интересно
            // case AF_INET6: {
            //     uint8_t ipv6[16];
            //     memcpy (ipv6, nl_addr_get_binary_addr(ipaddr), sizeof(ipv6));
            //     printf("%s route " IPv6_FMT"/%d\n", rtnl_link_get_name(link), IPv6_ARG(ipv6),dst_len);
            //   }
            //   break;

            default:
              break;
          }
        }
        // struct nl_addr *gw = rtnl_route_nh_get_gateway (nhop);

        // if(gw) {
        //   int siz = (route_get_family == AF_INET) ? 4 : (route_get_family == AF_INET6) ? 16 : 0;
        //   memcpy (&nexthop_data[i].gw, nl_addr_get_binary_addr (gw), siz);
        //   printf("tun route %s\n", rtnl_link_get_name(iface));
        // }
      }
    }
  }

	nl_object_dump(obj, &dp);
}

int main()
{
    dp.dp_fd = stdout;

	signal(SIGINT, sigint);
    sock = nl_socket_alloc();

	int err = nl_cache_mngr_alloc(sock, NETLINK_ROUTE, NL_AUTO_PROVIDE, &mngr);
	if (err < 0)
		printf("Unable to allocate cache manager: %s\n", nl_geterror(err));

    if ((err = nl_cache_mngr_add(mngr, "route/link", &link_cb, NULL, &lc)) < 0)
		printf("Unable to add cache route/link: %s\n",
		      nl_geterror(err));

	if ((err = nl_cache_mngr_add(mngr, "route/neigh", &neigh_cb, NULL, &nc)) < 0)
		printf("Unable to add cache route/neigh: %s\n",
		      nl_geterror(err));

	if ((err = nl_cache_mngr_add(mngr, "route/addr", &addr_cb, NULL, &ac)) < 0)
		printf("Unable to add cache route/addr: %s\n",
		      nl_geterror(err));

	if ((err = nl_cache_mngr_add(mngr, "route/route", &route_cb, NULL, &rc)) < 0)
		printf("Unable to add cache route/route: %s\n",
		      nl_geterror(err));

	while (!quit) {
		int err = nl_cache_mngr_poll(mngr, 1000);
		if (err < 0 && err != -NLE_INTR)
			printf("Polling failed: %s\n", nl_geterror(err));

		// nl_cache_mngr_info(mngr, &dp);
	}

	nl_cache_mngr_free(mngr);

	return 0;
}