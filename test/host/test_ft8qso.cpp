// Host-side test: g++ -std=c++17 -I src test/host/test_ft8qso.cpp -o test/host/t.out && test/host/t.out
#include <cstdio>
#include "lib/Ft8Qso.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main() {
    bool logNow = false;
    // We answer a CQ.
    ft8::Qso q;
    q.myCall = "TA3BLD"; q.myGrid = "KM38nk";
    q.answer("DX1ABC", -7);
    CHECK(q.message() == "DX1ABC TA3BLD KM38");
    CHECK(!q.received("CQ DX1ABC FN42", -7, logNow));                  // not for us
    CHECK(!q.received("TA3BLD OTHER1 -10", -3, logNow));               // another station
    CHECK(q.received("TA3BLD DX1ABC -12", -8, logNow) && !logNow);
    CHECK(q.message() == "DX1ABC TA3BLD R-07" && q.reportRcvd == -12);
    CHECK(q.received("TA3BLD DX1ABC RR73", -6, logNow) && logNow);
    CHECK(q.message() == "DX1ABC TA3BLD 73");
    q.transmitted();
    CHECK(q.step == ft8::Qso::DONE && q.message().empty());

    // We call CQ; a station answers with its grid.
    q.callCq();
    CHECK(q.message() == "CQ TA3BLD KM38" && q.dxCall.empty());
    CHECK(q.received("TA3BLD K1ABC FN42", -15, logNow) && !logNow);
    CHECK(q.dxCall == "K1ABC" && q.dxGrid == "FN42" && q.message() == "K1ABC TA3BLD -15");
    CHECK(!q.received("TA3BLD K1ABC FN42", -14, logNow));               // repeated: no progress
    CHECK(q.received("TA3BLD K1ABC R-09", -14, logNow) && logNow);
    CHECK(q.message() == "K1ABC TA3BLD RR73");
    CHECK(q.received("TA3BLD K1ABC 73", -14, logNow) && !logNow && q.step == ft8::Qso::DONE);

    // A caller that skips the grid and sends a report straight away.
    q.callCq();
    CHECK(q.received("TA3BLD JA1XYZ -20", -18, logNow) && q.message() == "JA1XYZ TA3BLD R-18");
    CHECK(q.received("TA3BLD JA1XYZ RRR", -17, logNow) && logNow && q.message() == "JA1XYZ TA3BLD 73");

    // Reports are clamped to what FT8 can carry; free text and hashes are ignored.
    q.answer("VK2DX", -35);
    CHECK(q.message() == "VK2DX TA3BLD KM38");
    CHECK(q.received("TA3BLD VK2DX +12", 3, logNow) && q.message() == "VK2DX TA3BLD R-30");
    q.callCq();
    CHECK(!q.received("TNX 73 GL", 0, logNow) && !q.received("TA3BLD <...> -10", 0, logNow));
    for (const char* m : {"DX1ABC TA3BLD KM38", "DX1ABC TA3BLD R-07", "K1ABC TA3BLD RR73", "CQ TA3BLD KM38"}) {
        uint8_t p[10];
        CHECK(ft8::pack(m, p) && ft8::getBits(p, 74, 3) == 1);         // all are standard messages
    }

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
