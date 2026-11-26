// Command line helper for tools/sstv/crosscheck.py:
//   sstv_tool encode "Martin M1" in.ppm out.raw RATE     (int16 mono)
//   sstv_tool decode in.raw RATE out_prefix               (writes out_prefix-N.ppm, prints "mode lines slant_ppm")
// Build: g++ -std=c++17 -O2 -I src test/host/sstv_tool.cpp -o sstv_tool
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "lib/Sstv.h"

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

static bool readPpm(const char* path, Image& img) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    int maxv;
    if (fscanf(f, "P6 %d %d %d", &img.w, &img.h, &maxv) != 3) return false;
    fgetc(f);
    img.rgb.resize((size_t)img.w * img.h * 3);
    size_t got = fread(img.rgb.data(), 1, img.rgb.size(), f);
    fclose(f);
    return got == img.rgb.size();
}

static void writePpm(const std::string& path, const Image& img) {
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
    fwrite(img.rgb.data(), 1, img.rgb.size(), f);
    fclose(f);
}

static bool source(void* user, int y, uint8_t* rgb) {
    Image& img = *(Image*)user;
    if (y >= img.h) return false;
    std::copy(img.rgb.begin() + (long)y * img.w * 3, img.rgb.begin() + (long)(y + 1) * img.w * 3, rgb);
    return true;
}

struct Rx {
    std::vector<Image> images;
    int lastY = -1;
};

static void sink(void* user, const sstv::Mode& m, int y, const uint8_t* rgb) {
    Rx& rx = *(Rx*)user;
    if (y == 0 || rx.images.empty() || y < rx.lastY) {
        Image img;
        img.w = m.width;
        img.h = m.height;
        img.rgb.assign((size_t)m.width * m.height * 3, 0);
        rx.images.push_back(img);
    }
    Image& img = rx.images.back();
    std::copy(rgb, rgb + m.width * 3, img.rgb.begin() + (long)y * m.width * 3);
    rx.lastY = y;
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "encode" && argc == 6) {
        size_t n;
        const sstv::Mode* all = sstv::modes(n);
        const sstv::Mode* m = nullptr;
        for (size_t i = 0; i < n; i++)
            if (argv[2] == std::string(all[i].name)) m = &all[i];
        Image img;
        if (!m || !readPpm(argv[3], img) || img.w != m->width) { fprintf(stderr, "bad mode or image\n"); return 1; }
        sstv::Encoder enc(*m, source, &img, atoi(argv[5]));
        FILE* f = fopen(argv[4], "wb");
        int16_t buf[1024];
        for (size_t k; (k = enc.read(buf, 1024)) > 0;) fwrite(buf, 2, k, f);
        fclose(f);
        return 0;
    }
    if (mode == "decode" && argc == 5) {
        FILE* f = fopen(argv[2], "rb");
        if (!f) return 1;
        std::vector<int16_t> audio;
        int16_t s;
        while (fread(&s, 2, 1, f) == 1) audio.push_back(s);
        fclose(f);
        sstv::Decoder dec(atoi(argv[3]));
        Rx rx;
        dec.onLine(sink, &rx);
        const sstv::Mode* last = nullptr;
        double slant = 0;
        for (size_t i = 0; i < audio.size(); i += 256) {
            dec.feed(audio.data() + i, std::min<size_t>(256, audio.size() - i));
            if (dec.mode()) { last = dec.mode(); slant = dec.slantPpm(); }
        }
        for (size_t i = 0; i < rx.images.size(); i++) writePpm(std::string(argv[4]) + "-" + std::to_string(i) + ".ppm", rx.images[i]);
        printf("%s %d %.0f %d\n", last ? last->name : "none", dec.lines(), slant, dec.completed());
        return 0;
    }
    fprintf(stderr, "usage: see the top of sstv_tool.cpp\n");
    return 2;
}
