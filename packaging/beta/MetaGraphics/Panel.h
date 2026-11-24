// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Picker.h"
#include <windows.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace HarleyGraphics {
constexpr int width = 1024, height = 576;

inline std::vector<std::uint32_t> PanelPixels(int index, bool bgra) {
    struct Drawing {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = nullptr;
        HGDIOBJ original = nullptr;
        ~Drawing() {
            if (original && original != HGDI_ERROR) SelectObject(dc, original);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    } d;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    if (!d.dc || !(d.bitmap = CreateDIBSection(d.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0)) || !bits)
        throw std::runtime_error("Could not allocate the graphics menu image.");
    d.original = SelectObject(d.dc, d.bitmap);
    if (!d.original || d.original == HGDI_ERROR) throw std::runtime_error("Could not draw the graphics menu.");
    auto fill = [&](RECT area, COLORREF color) {
        SetDCBrushColor(d.dc, color);
        if (!FillRect(d.dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH))))
            throw std::runtime_error("Could not fill the graphics menu.");
    };
    auto label = [&](const wchar_t* value, RECT area, int size, int weight, COLORREF color, UINT align = DT_LEFT) {
        HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font) throw std::runtime_error("Could not create the graphics menu text.");
        auto previous = SelectObject(d.dc, font);
        SetTextColor(d.dc, color);
        const auto result = DrawTextW(d.dc, value, -1, &area, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | align);
        SelectObject(d.dc, previous);
        DeleteObject(font);
        if (!result) throw std::runtime_error("Could not draw the graphics menu text.");
    };
    constexpr auto blue = RGB(15,43,66), light = RGB(239,245,250), gold = RGB(255,207,91);
    fill({0,0,width,height},blue);
    fill({0,0,width,10},gold);
    SetBkMode(d.dc, TRANSPARENT);
    label(L"HarleyVR  /  Meta PC VR", {48,30,978,78}, 29, FW_SEMIBOLD, gold);
    label(L"Headset render quality", {48,84,978,172}, 62, FW_BOLD, light);
    label(L"Scale per eye for this ride", {50,170,978,223}, 34, FW_NORMAL, light);
    constexpr const wchar_t* names[] = {L"Performance",L"Balanced",L"Quality"};
    constexpr const wchar_t* values[] = {L"50%",L"75%",L"100%"};
    for (int i=0; i<3; ++i) {
        const int x=48+i*314;
        const auto c = i==index ? blue : light;
        fill({x,251,x+299,398},i==index ? gold : RGB(37,72,98));
        label(names[i], {x+3,260,x+296,321}, 37, FW_SEMIBOLD, c, DT_CENTER);
        label(values[i], {x+3,320,x+296,386}, 47, FW_BOLD, c, DT_CENTER);
    }
    label(L"Right stick   Choose quality", {49,422,975,478}, 35, FW_NORMAL, light);
    label(L"A   Start ride       B   Cancel", {49,478,975,540}, 36, FW_SEMIBOLD, gold);
    GdiFlush();
    auto raw = static_cast<const std::uint32_t*>(bits);
    std::vector<std::uint32_t> pixels(raw,raw+width*height);
    for (auto& p : pixels) {
        if (!bgra) p=((p & 0xffu)<<16)|(p & 0xff00u)|((p>>16)&0xffu);
        p |= 0xff000000u;
    }
    return pixels;
}
}
