#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "libslic3r/FlushVolCalc.hpp"

// ---------------------------------------------------------------------------
// 测试侧 RGB2HSV
//
// libslic3r 的 FlushVolCalc.cpp 调用了自由函数 RGB2HSV()，其定义位于
// src/slic3r/Utils/ColorSpaceConvert.cpp，并被编译进 libslic3r_gui 库。
// 而 libslic3r_tests 只链接 libslic3r（不含该库），导致该符号链接时无法解析。
// 这里提供一份纯数学实现（与生产实现保持一致），使测试能够通过链接。
// 若生产侧 RGB2HSV 有改动，请同步更新此处副本。
// ---------------------------------------------------------------------------
void RGB2HSV(float r, float g, float b, float* h, float* s, float* v)
{
    float Cmax  = std::max(std::max(r, g), b);
    float Cmin  = std::min(std::min(r, g), b);
    float delta = Cmax - Cmin;

    if (std::abs(delta) < 0.001) {
        *h = 0.f;
    }
    else if (Cmax == r) {
        *h = 60.f * std::fmod((g - b) / delta, 6.f);
    }
    else if (Cmax == g) {
        *h = 60.f * ((b - r) / delta + 2);
    }
    else {
        *h = 60.f * ((r - g) / delta + 4);
    }

    if (std::abs(Cmax) < 0.001) {
        *s = 0.f;
    }
    else {
        *s = delta / Cmax;
    }

    *v = Cmax;
}

using namespace Slic3r;

namespace {

struct Rgba {
    const char*    name;
    unsigned char a, r, g, b;
};

const Rgba kColors[] = {
    {"red",     255, 255, 0,   0},
    {"green",   255, 0,   255, 0},
    {"blue",    255, 0,   0,   255},
    {"yellow",  255, 255, 255, 0},
    {"cyan",    255, 0,   255, 255},
    {"magenta", 255, 255, 0,   255},
    {"white",   255, 255, 255, 255},
    {"black",   255, 0,   0,   0},
};

// 便捷包装：以 min=0、max=g_max_flush_volume 构造计算器并计算。
int calc(unsigned char sa, unsigned char sr, unsigned char sg, unsigned char sb,
         unsigned char da, unsigned char dr, unsigned char dg, unsigned char db)
{
    return FlushVolCalculator(0, g_max_flush_volume)
        .calc_flush_vol(sa, sr, sg, sb, da, dr, dg, db);
}

} 

SCENARIO("FlushVolCalc: deterministic golden values", "[FlushVolCalc]") {
    GIVEN("min=0、max=800 的计算器") {
        THEN("同色结果等于下限 min + 60") {
            CHECK(calc(255, 0, 0, 0, 255, 0, 0, 0) == 60);            // 黑 -> 黑
            CHECK(calc(255, 255, 0, 0, 255, 255, 0, 0) == 60);        // 红 -> 红
        }

        THEN("切到更亮的颜色需要更多冲刷（不对称）") {
            CHECK(calc(255, 0, 0, 0, 255, 255, 255, 255) == 560);     // 黑 -> 白
            CHECK(calc(255, 255, 255, 255, 255, 0, 0, 0) == 80);      // 白 -> 黑
        }

        THEN("透明色（alpha == 0）按白色处理") {
            CHECK(calc(0, 0, 0, 0, 255, 0, 0, 0) == 80);              // 透明 -> 黑
            CHECK(calc(0, 0, 0, 0, 255, 255, 255, 255) == 60);        // 透明 -> 白
        }
    }
}

SCENARIO("FlushVolCalc: min offset and max clamp", "[FlushVolCalc]") {
    GIVEN("min=420（支撑材料）的计算器") {
        FlushVolCalculator support(420, g_max_flush_volume);

        THEN("黑→白 560 + 420 = 980 被夹到 max=800") {
            CHECK(support.calc_flush_vol(255, 0, 0, 0, 255, 255, 255, 255) == 800);
        }
    }

    GIVEN("min=100 的计算器") {
        FlushVolCalculator raised(100, g_max_flush_volume);

        THEN("同色结果被抬高到 60 + 100 = 160") {
            CHECK(raised.calc_flush_vol(255, 0, 0, 0, 255, 0, 0, 0) == 160);
        }
    }
}

SCENARIO("FlushVolCalc: color pair golden matrix", "[FlushVolCalc]") {
    GIVEN("探针捕获的 8x8 golden 矩阵") {
        FlushVolCalculator calc(0, g_max_flush_volume);
        const int           expected[8][8] = {
            { 60, 443, 237, 540, 494, 307, 586,  90},   // red
            {242,  60, 251, 408, 307, 237, 460, 107},   // green
            {393, 529,  60, 653, 540, 408, 661,  80},   // blue
            {256, 242, 266,  60, 237, 251, 307, 127},   // yellow
            {247, 234, 256, 393,  60, 242, 408, 114},   // cyan
            {234, 388, 242, 529, 443,  60, 540,  96},   // magenta
            {262, 248, 272, 234, 242, 256,  60,  80},   // white
            {408, 540, 307, 661, 586, 460, 560,  60},   // black
        };

        THEN("每个源→目标颜色对的结果都等于捕获值") {
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 8; ++j) {
                    DYNAMIC_SECTION(kColors[i].name << " -> " << kColors[j].name) {
                        CHECK(calc.calc_flush_vol(kColors[i].a, kColors[i].r, kColors[i].g, kColors[i].b,
                                                  kColors[j].a, kColors[j].r, kColors[j].g, kColors[j].b)
                              == expected[i][j]);
                    }
                }
        }
    }
}

SCENARIO("FlushVolCalc: alpha independence and bounds", "[FlushVolCalc]") {
    GIVEN("min=0、max=800 的计算器") {
        FlushVolCalculator calc(0, g_max_flush_volume);

        THEN("alpha > 0 的任意取值等价（仅 alpha == 0 被特殊处理）") {
            CHECK(calc.calc_flush_vol(255, 255, 0, 0, 255, 0, 0, 255) ==
                  calc.calc_flush_vol(128, 255, 0, 0, 255, 0, 0, 255));
        }

        THEN("所有颜色对的结果都落在 [min + 60, max] 区间") {
            for (const Rgba& s : kColors)
                for (const Rgba& d : kColors) {
                    int v = calc.calc_flush_vol(s.a, s.r, s.g, s.b, d.a, d.r, d.g, d.b);
                    CHECK(v >= 60);
                    CHECK(v <= g_max_flush_volume);
                }
        }
    }
}
