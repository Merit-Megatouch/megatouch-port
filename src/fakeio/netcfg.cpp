// netcfg — read or change the cabinet's network settings through its own network library
// (libnetwork.so: network::settings::Interface), the way the Network menu does. The settings live
// in the cabinet's encrypted configuration database, so they are only changed through this code.
// Runs inside the loader sandbox, with the cabinet stopped (both write the same database):
//
//   scripts/loader.sh run /opt/fakeio/netcfg                       show
//   scripts/loader.sh run /opt/fakeio/netcfg meganet-id ID         set the MegaNet ID
//   scripts/loader.sh run /opt/fakeio/netcfg server NAME           set the MegaNet server
//   scripts/loader.sh run /opt/fakeio/netcfg megalink-id ID        set the MegaLink ID
//
// Built against the cabinet's library with the old (pre-C++11) std::string ABI it was compiled
// with. Only the methods used here are declared; the object gets plenty of room.
#include <cstdio>
#include <cstring>
#include <string>

namespace network { namespace settings {
class Interface {
public:
    Interface();
    ~Interface();
    std::string get_meganet_machine_id();
    std::string get_meganet_server_name();
    std::string get_megalink_id();
    void set_meganet_machine_id(std::string);
    void set_meganet_server_name(std::string);
    void set_megalink_id(std::string);
    void save_settings();
private:
    char room_[16384];   // larger than the real object
};
}}

int main(int argc, char **argv) {
    network::settings::Interface net;
    if (argc == 3) {
        std::string what = argv[1], value = argv[2];
        if (what == "meganet-id") net.set_meganet_machine_id(value);
        else if (what == "server") net.set_meganet_server_name(value);
        else if (what == "megalink-id") net.set_megalink_id(value);
        else { fprintf(stderr, "unknown setting: %s\n", argv[1]); return 2; }
        net.save_settings();
    } else if (argc != 1) {
        fprintf(stderr, "usage: netcfg [meganet-id ID | server NAME | megalink-id ID]\n");
        return 2;
    }
    printf("meganet-id %s\n", net.get_meganet_machine_id().c_str());
    printf("server %s\n", net.get_meganet_server_name().c_str());
    printf("megalink-id %s\n", net.get_megalink_id().c_str());
    return 0;
}
