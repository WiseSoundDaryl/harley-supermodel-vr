// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panel.h"
#include <algorithm>
#include <iostream>
#include <limits>

int main() {
    int count=0;
    auto expect=[&](bool ok) { if (!ok) throw std::runtime_error("Meta graphics check "+std::to_string(count+1)+" failed."); ++count; };
    expect(HarleyGraphics::PresetIndex("Performance")==0 && HarleyGraphics::PresetIndex("Balanced")==1 && HarleyGraphics::PresetIndex("Quality")==2);
    expect(HarleyGraphics::PresetIndex("quality")==-1 && HarleyGraphics::PresetIndex("")==-1);
    for (int level=0; level<3; ++level) {
        HarleyGraphics::Selection menu{level};
        expect(menu.Update(true,0,0,true,false)==-1);
        expect(menu.Update(true,0,0,false,false)==-1);
        expect(menu.Update(true,0,0,true,false)==HarleyGraphics::scales[level]);
        expect(menu.Update(true,0,0,true,false)==-1);
        menu.Update(true,0,0,false,false);
        expect(menu.Update(true,0,0,true,true)==0);
    }
    HarleyGraphics::Selection menu{1};
    menu.Update(true,0,0,false,false); menu.Update(true,.68f,0,false,false);
    expect(menu.index==2);
    menu.Update(true,-1,0,false,false); expect(menu.index==2);
    menu.Update(true,.4f,0,false,false); menu.Update(true,-1,0,false,false); expect(menu.index==2);
    menu.Update(true,.1f,0,false,false); menu.Update(true,0,-1,false,false); expect(menu.index==1);
    menu.Update(true,0,0,false,false); menu.Update(true,0,-1,false,false); expect(menu.index==0);
    menu.Update(true,0,0,false,false); menu.Update(true,0,-1,false,false); expect(menu.index==0);
    menu.Update(true,0,0,false,false); menu.Update(false,0,0,false,false);
    expect(menu.Update(true,0,0,false,true)==-1);
    menu.Update(true,0,0,false,false); menu.Update(true,std::numeric_limits<float>::quiet_NaN(),0,false,false);
    expect(!menu.armed);
    // Rasterize into memory only. Compare selected backgrounds and the exact
    // packing submitted to the two supported channel orders; no window or XR.
    for (int selected=0; selected<3; ++selected) {
        auto bgra=HarleyGraphics::PanelPixels(selected,true);
        auto rgba=HarleyGraphics::PanelPixels(selected,false);
        expect(bgra.size()==HarleyGraphics::width*HarleyGraphics::height && rgba.size()==bgra.size());
        for (int card=0; card<3; ++card) {
            size_t pos=254*HarleyGraphics::width+51+314*card;
            expect(bgra[pos]==(selected==card?0xffffcf5b:0xff254862));
        }
        expect(std::equal(bgra.begin(),bgra.end(),rgba.begin(),[](auto a,auto b){
            return (a>>24)==255 && (b>>24)==255 && ((a>>16)&255)==(b&255) &&
                   (a&255)==((b>>16)&255) && ((a>>8)&255)==((b>>8)&255);
        }));
        // The title and instructions contain actual high-contrast glyphs.
        for (auto band : {std::pair<int,int>{90,170},{428,478},{480,540}}) {
            auto first=bgra.begin()+band.first*HarleyGraphics::width;
            auto last=bgra.begin()+band.second*HarleyGraphics::width;
            expect(std::count_if(first,last,[](auto p){return ((p>>16)&255)>220;})>300);
        }
    }
    std::cout << count << " offline graphics picker checks passed.\n";
}
