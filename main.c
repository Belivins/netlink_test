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

static void change_cb(struct nl_cache *cache, struct nl_object *obj,
		      int action, void *data)
{
	if (action == NL_ACT_NEW)
		printf("NEW ");
	else if (action == NL_ACT_DEL)
		printf("DEL ");
	else if (action == NL_ACT_CHANGE)
		printf("CHANGE ");

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

    // int err = nl_cache_mngr_alloc(sock, )

	int err = nl_cache_mngr_alloc(sock, NETLINK_ROUTE, NL_AUTO_PROVIDE, &mngr);
	if (err < 0)
		printf("Unable to allocate cache manager: %s", nl_geterror(err)); 

    if ((err = nl_cache_mngr_add(mngr, "route/link", &change_cb, NULL, &lc)) < 0)
		printf("Unable to add cache route/link: %s",
		      nl_geterror(err));

	if ((err = nl_cache_mngr_add(mngr, "route/neigh", &change_cb, NULL, &nc)) < 0)
		printf("Unable to add cache route/neigh: %s",
		      nl_geterror(err));

	if ((err = nl_cache_mngr_add(mngr, "route/addr", &change_cb, NULL, &ac)) < 0)
		printf("Unable to add cache route/addr: %s",
		      nl_geterror(err));

	if ((err = nl_cache_mngr_add(mngr, "route/route", &change_cb, NULL, &rc)) < 0)
		printf("Unable to add cache route/route: %s",
		      nl_geterror(err));

	while (!quit) {
		int err = nl_cache_mngr_poll(mngr, 1000);
		if (err < 0 && err != -NLE_INTR)
			printf("Polling failed: %s", nl_geterror(err));

		// nl_cache_mngr_info(mngr, &dp);
	}

	nl_cache_mngr_free(mngr);

	return 0;
}