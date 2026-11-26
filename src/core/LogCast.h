// Sends each logged contact to a PC logging program over the network
// (settings "Log to PC" = host:port, "Log to PC format" = n1mm / adif).
// A host ending in .255 broadcasts to the whole network.
#pragma once
#include "lib/Adif.h"

namespace logcast {
void begin();                       // hooks into the logbook
void send(const adif::Qso& q);
unsigned sentCount();
}
