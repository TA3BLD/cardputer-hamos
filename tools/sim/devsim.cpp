// A HamOS "device" on the PC for trying the PC program without hardware:
// the real firmware code runs in real time with the simulator's stand-in
// hardware, and its USB serial port is a pseudo terminal.
//
//   g++ -std=gnu++17 -O1 -DHAMOS_SIM -I stubs -I ../../src devsim.cpp ../../src/core/*.cpp -lcrypto -lutil -o devsim
//   ./devsim            (prints the port, e.g. /dev/pts/5; run it inside a folder with simsd/HamOS)
//   python ../pc/hamos_pc.py --port /dev/pts/5
//
// With a port number as argument (./devsim 8080) it also answers like the
// device's WiFi web server: http://127.0.0.1:8080/ is the control panel the
// Cardputer serves itself (core/Web.cpp: "/", "/api", "/screen").
#include "main.cpp"
#include "core/WebPage.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <RadioLib.h>
#include "lib/Meshtastic.h"
#include "mbedtls/aes.h"

uint32_t sim_ms = 0;
static long long start_ms = 0;
static long long wallMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
long long sim_utc_ms() { return start_ms + sim_ms; }

static int master = -1;
size_t sim_usb_write(const uint8_t* data, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t w = ::write(master, data + done, n - done);
        if (w > 0) done += (size_t)w;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return n;
}

// ---- some life on the bands ----
static std::string nmeaLine(const std::string& body) {
    uint8_t sum = 0;
    for (char c : body) sum ^= (uint8_t)c;
    char tail[8];
    snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
    return "$" + body + tail;
}

struct Aes : meshtastic::BlockCipher {
    mbedtls_aes_context ctx;
    Aes() { mbedtls_aes_setkey_enc(&ctx, meshtastic::kDefaultKey, 128); }
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) override { mbedtls_aes_crypt_ecb(&ctx, 1, in, out); }
};

static void meshPacket(uint32_t from, int port, const std::string& payload) {
    static Aes aes;
    static uint32_t id = 0x2000;
    meshtastic::Header h;
    h.from = from;
    h.id = id++;
    h.channel = 0x08;
    meshtastic::Data d;
    d.portnum = port;
    d.payload = payload;
    std::string body = meshtastic::encodeData(d);
    meshtastic::cryptCtr(aes, h.from, h.id, (uint8_t*)&body[0], body.size());
    uint8_t head[meshtastic::HEADER_LEN];
    meshtastic::packHeader(h, head);
    sim_lora_inject(std::string((const char*)head, sizeof(head)) + body);
}

static void meshNode(uint32_t node, const char* longName, const char* shortName, double lat, double lon) {
    meshtastic::User u;
    u.id = meshtastic::nodeIdString(node);
    u.longName = longName;
    u.shortName = shortName;
    meshPacket(node, meshtastic::PORT_NODEINFO, meshtastic::encodeUser(u));
    for (int i = 0; i < 5; i++) loop();
    meshtastic::Position p;
    p.hasLatLon = true;
    p.lat = lat;
    p.lon = lon;
    meshPacket(node, meshtastic::PORT_POSITION, meshtastic::encodePosition(p));
    for (int i = 0; i < 5; i++) loop();
}

// ---- a small stand-in for the device's web server ----
static int webListen = -1;

static void webStart(int port) {
    webListen = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(webListen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(webListen, (sockaddr*)&a, sizeof(a)) != 0 || listen(webListen, 8) != 0) { perror("web"); exit(1); }
    fcntl(webListen, F_SETFL, fcntl(webListen, F_GETFL) | O_NONBLOCK);
    fprintf(stderr, "web server on http://127.0.0.1:%d/\n", port);
}

static void webReply(int fd, const char* type, const std::string& body, bool gzip = false) {
    std::string head = std::string("HTTP/1.1 200 OK\r\nContent-Type: ") + type + "\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n" +
                       (gzip ? "Content-Encoding: gzip\r\n" : "") + "\r\n";
    std::string all = head + body;
    size_t done = 0;
    while (done < all.size()) {
        ssize_t n = ::write(fd, all.data() + done, all.size() - done);
        if (n <= 0) break;
        done += (size_t)n;
    }
}

static void webTick() {
    if (webListen < 0) return;
    int fd = accept(webListen, nullptr, nullptr);
    if (fd < 0) return;
    std::string req;
    char b[4096];
    size_t need = std::string::npos;
    for (int tries = 0; tries < 2000; tries++) {        // one request, with its body
        ssize_t n = ::read(fd, b, sizeof(b));
        if (n > 0) req.append(b, (size_t)n);
        size_t end = req.find("\r\n\r\n");
        if (end != std::string::npos) {
            if (need == std::string::npos) {
                size_t cl = req.find("Content-Length: ");
                need = end + 4 + (cl != std::string::npos && cl < end ? (size_t)atoi(req.c_str() + cl + 16) : 0);
            }
            if (req.size() >= need) break;
        }
        if (n == 0) break;
    }
    size_t end = req.find("\r\n\r\n");
    std::string body = end == std::string::npos ? "" : req.substr(end + 4);
    if (req.rfind("GET / ", 0) == 0) webReply(fd, "text/html; charset=utf-8", std::string((const char*)web::kPageGz, web::kPageGzLen), true);
    else if (req.rfind("POST /api ", 0) == 0) webReply(fd, "application/json", usb::handle(body, true));
    else if (req.rfind("GET /screen ", 0) == 0) webReply(fd, "text/plain", usb::webScreen());
    else {
        static const char kNotFound[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (::write(fd, kNotFound, sizeof(kNotFound) - 1) < 0) {}
    }
    close(fd);
}

int main(int argc, char** argv) {
    int slave;
    char name[128];
    if (openpty(&master, &slave, name, nullptr, nullptr) != 0) { perror("openpty"); return 1; }
    struct termios t;
    tcgetattr(slave, &t);
    cfmakeraw(&t);
    tcsetattr(slave, TCSANOW, &t);
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    fprintf(stderr, "HamOS device simulator: USB serial port is %s\n", name);
    printf("%s\n", name);
    fflush(stdout);
    if (!freopen("/dev/null", "w", stdout)) return 1;           // the stub display prints drawing commands

    if (argc > 1) webStart(atoi(argv[1]));
    start_ms = wallMs();
    setup();
    sim_net["rotate.aprs2.net"].inject("# aprsc\r\nTA3XYZ-9>APDR16,TCPIP*,qAC,T2TURKEY:=3827.60N/02706.60E>Karsiyaka sahil\r\n"
                                       "TA3ABC-1>APRS,TCPIP*,qAC,T2TURKEY:!3828.20N/02713.20E#Bornova digi\r\n"
                                       "YM3KDS>APRS,qAR,TA3ABC-1:!3813.70N/02650.10E-Seferihisar\r\n"
                                       "TA3XYZ-9>APDR16,TCPIP*::TA3BLD-7 :Selam, PC'den goruyor musun?{9\r\n");
    meshNode(0xa1b2c3d4, "TA3XYZ Karsiyaka", "XYZ", 38.46, 27.11);
    meshNode(0x0badcafe, "Bornova Repeater", "BRN", 38.47, 27.22);
    meshPacket(0xa1b2c3d4, meshtastic::PORT_TEXT, "Merhaba, kanalda kimse var mı?");

    long long lastGps = 0;
    char buf[4096];
    while (true) {
        sim_ms = (uint32_t)(wallMs() - start_ms);
        ssize_t n = ::read(master, buf, sizeof(buf));
        if (n > 0) Serial.inject(std::string(buf, (size_t)n));
        if (sim_ms - lastGps >= 1000) {
            lastGps = sim_ms;
            Serial1.inject(nmeaLine("GNGGA,110900.00,3825.422,N,02708.568,E,1,08,1.1,121.0,M,36.0,M,,"));
            Serial1.inject(nmeaLine("GNRMC,110900.00,A,3825.422,N,02708.568,E,0.4,271.0,131126,,,A"));
        }
        loop();
        webTick();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
