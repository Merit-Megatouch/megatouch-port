// Networking stand-ins: a single, unlinked cabinet (NetLink::INetUtils / INetLink).

#include "backend.h"
#include <cstring>
#include <list>

// ---------------------------------------------------------------------------
// NetUtils: a single, unlinked machine.

static void** g_netVtbl;
static void net_noop(void*) {}
static void net_update(void* self, float dt) { at<float>(self, 8) += dt; }
static void net_sync(void* self) { at<float>(self, 8) = 0; }
static float net_timer(void* self) { return at<float>(self, 8); }
static int net_zero(void*) { return 0; }
static void net_wait(void*, unsigned long, bool) {}
static bool net_check(void*, int, int) { return true; }
static void net_disconnect(void*, void*) {}
static std::list<int> net_list(void*) { return {}; }

void* net_new() {
    if (!g_netVtbl) {
        g_netVtbl = clone_vtable("_ZTVN7NetLink9INetUtilsE");
        g_netVtbl[2] = (void*)net_noop;          // Initialize
        g_netVtbl[3] = (void*)net_update;        // Update(float)
        g_netVtbl[4] = (void*)net_sync;          // SyncTimers
        g_netVtbl[5] = (void*)net_timer;         // GetTimer
        g_netVtbl[6] = (void*)net_zero;          // GetNumUnitsLinked
        g_netVtbl[7] = (void*)net_wait;          // WaitForAllMachinesToRespond
        g_netVtbl[8] = (void*)net_check;         // CheckStateOfMachine
        g_netVtbl[9] = (void*)net_disconnect;    // ForceDisconnect
        g_netVtbl[10] = (void*)net_zero;         // GetMyLinkId
        g_netVtbl[11] = (void*)net_zero;         // GetMasterLinkId
        g_netVtbl[12] = (void*)net_list;         // GetAllLinkedIds
        g_netVtbl[13] = (void*)net_list;         // GetHeadIndices
    }
    void* n = operator new(0x10);
    memset(n, 0, 0x10);
    reinterpret_cast<ctor0_t>(sym("_ZN7NetLink9INetUtilsC2Ev"))(n);
    at<void**>(n, 0) = g_netVtbl;
    return n;
}

// NetLink::INetLink (+4 = registered NetMessageManager). Broadcasts would go to other
// linked cabinets; there are none, so they are dropped.
static void** g_linkVtbl;
static void link_broadcast(void*, void*, unsigned char) {}
static void link_noop(void*) {}
static void link_click(void*, unsigned char) {}
void* netlink_new() {
    if (!g_linkVtbl) {
        g_linkVtbl = clone_vtable("_ZTVN7NetLink8INetLinkE", 2);
        g_linkVtbl[2] = (void*)link_broadcast;
        g_linkVtbl[3] = (void*)link_noop;    // ForceDisconnect
        g_linkVtbl[4] = (void*)link_click;   // NetClick
    }
    void* l = operator new(8);
    reinterpret_cast<ctor0_t>(sym("_ZN7NetLink8INetLinkC2Ev"))(l);
    at<void**>(l, 0) = g_linkVtbl;
    return l;
}
