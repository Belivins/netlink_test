#include <stdio.h>
#include <signal.h>
#include <netlink/socket.h>
#include <netlink/cache.h>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/route.h>
#include <netlink/cli/utils.h>


static int quit = 0;

static struct nl_dump_params dp = {
	.dp_type = NL_DUMP_LINE,
};

static void sigint(int arg)
{
	quit = 1;
}

static void link_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
  printf("%s\n", __func__);
	nl_object_dump(obj, &dp);
}

static void neigh_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
  printf("%s\n", __func__);
	nl_object_dump(obj, &dp);
}

static void addr_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
  printf("%s\n", __func__);
	nl_object_dump(obj, &dp);
}

static void route_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
  printf("%s\n", __func__);
	nl_object_dump(obj, &dp);
}

int main()
{
	struct nl_cache_mngr *mngr;
    struct nl_cache *lc, *nc, *ac, *rc;
    struct nl_sock *sock;
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