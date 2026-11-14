#include "Settings.h"
#include "AprsIs.h"
#include "LoraAprs.h"
#include "Mesh.h"
#include "Net.h"
#include "Rig.h"
#include "Wsjtx.h"

namespace settings {

void apply(int what) {
    if (what & WIFI) net::begin();
    if (what & MESH) { mesh::begin(); loraaprs::begin(); }
    if (what & APRS) aprsis::begin();
    if (what & RIG) rig::begin();
    if (what & FT8) wsjtxlink::begin();
}

}  // namespace settings
