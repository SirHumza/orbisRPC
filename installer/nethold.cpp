/* nethold.cpp - the C-callable side of netclient.
 *
 * installer.c is C and cannot include netclient.h, because that header pulls in
 * <string>. These two functions give the flow a way to hold the network context
 * open without the C file needing to know that netclient is C++ at all.
 *
 * They also give the refcount one owner. net::Init/Term are refcounted inside
 * netclient, but the flow wants one acquire spanning several calls, and a
 * separate counter here is what makes "one context for the whole flow" something
 * the C side can actually express.
 */

#include "netclient.h"

/* Nesting depth for the flow's own acquire. Kept distinct from netclient's
 * internal g_refs: this one tracks what the wizard asked for, which is not the
 * same question as how many HTTP requests are outstanding. */
static int g_flowRefs = 0;

extern "C" int net_flow_hold(void)
{
	g_flowRefs++;
	net::Init();
	return g_flowRefs;
}

extern "C" void net_flow_release(void)
{
	if (g_flowRefs <= 0)
		return;

	if (--g_flowRefs == 0)
		net::Term();
}
