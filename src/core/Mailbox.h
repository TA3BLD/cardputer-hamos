// Mesh mailbox (store and forward), setting "mesh_mailbox" = 1.
// A channel message that starts with "@NAME " (a node's short name, long name
// or !id) for a node not heard in the last 30 minutes is kept on the SD card;
// when that node is heard again, HamOS sends it to it as a direct message:
// "[mail from XYZ 14:05] text". Mail older than 48 hours is dropped.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mailbox {

struct Mail {
    uint32_t to = 0, from = 0;
    long long when = 0;         // unix time stored (0 if the clock was unknown)
    uint32_t storedMs = 0;
    std::string text;
};

void begin();                   // reads the stored mail
void tick();
const std::vector<Mail>& waiting();
uint32_t delivered();
void remove(size_t i);
// For the tests: the node a "@name" refers to, 0 if none.
uint32_t resolve(const std::string& name);

}  // namespace mailbox
