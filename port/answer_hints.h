#pragma once
// Button hints beside the game's answer boards (PC setting "button_hints").
// The boards are plain coloured geometry, not textures, so they are found in
// the draw stream (port/draw_observer.h) and the hint is drawn by the PC
// overlay. Verified on a captured question frame of the disc 1.02 build:
// the board frame shader carries k0=(-0.5,0,0,0), k1=k2=k3=(board colour, 1)
// and k4=(12,1,0.3,fade); its vertex program's clip matrix is c56..c59.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace buzz_hints {
// Handset order top to bottom, which is also the boards' order on screen.
enum Board : int { blue, orange, green, yellow, board_count };
inline constexpr float colors[board_count][3]={
    {0.0980392f,0.396078f,1.0f},{1.0f,0.517647f,0.0f},{0.282353f,1.0f,0.0f},{1.0f,0.917647f,0.0f}};
// Wire bit of each board's colour in buzz_input's red,yellow,green,orange,blue order.
inline constexpr unsigned wire_button[board_count]={4,3,2,1};

inline float as_float(uint32_t bits){float f;std::memcpy(&f,&bits,sizeof f);return f;}
// Board colour of a draw from its fragment constants (float4 bit patterns), or -1.
inline int classify(const uint32_t* k,uint32_t count){
    if(!k||count<5)return -1;
    auto at=[&](unsigned slot,unsigned c){return as_float(k[slot*4+c]);};
    auto close=[](float a,float b,float tolerance){return std::fabs(a-b)<=tolerance;};
    if(!close(at(0,0),-0.5f,1e-4f)||at(0,1)!=0||at(0,2)!=0||at(0,3)!=0)return -1;
    for(unsigned c=0;c<4;++c)if(k[4+c]!=k[8+c]||k[4+c]!=k[12+c])return -1;
    if(!close(at(1,3),1.0f,1e-3f))return -1;
    if(!close(at(4,0),12.0f,1e-3f)||!close(at(4,1),1.0f,1e-3f)||!close(at(4,2),0.3f,1e-3f))return -1;
    for(int b=0;b<board_count;++b)
        if(close(at(1,0),colors[b][0],0.02f)&&close(at(1,1),colors[b][1],0.02f)&&close(at(1,2),colors[b][2],0.02f))return b;
    return -1;
}
// A board's extent as a fraction of the 1280x720 guest display.
struct Box {float left,top,right,bottom;};
inline constexpr float display_height=720.0f;
// Projects attribute 0 through the rows c56..c59. A target shorter than the
// display is composed centred (the game renders 1280x704, shown 8 px down).
inline bool project(const uint32_t* vs,uint32_t vs_count,const uint8_t* vertices,uint32_t count,
                    uint32_t stride,int32_t position,float width,float height,Box& out){
    if(!vs||vs_count<60||!vertices||!count||position<0||stride<uint32_t(position)+16||width<=0||height<=0)return false;
    float m[4][4];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)m[r][c]=as_float(vs[(56+r)*4+c]);
    const float offset=height<display_height?(display_height-height)*0.5f:0.0f;
    float l=1e30f,t=1e30f,rgt=-1e30f,b=-1e30f;unsigned used=0;
    for(uint32_t i=0;i<count;++i){
        float p[3];std::memcpy(p,vertices+size_t(i)*stride+size_t(position),sizeof p);
        float clip[4];
        for(unsigned r=0;r<4;++r)clip[r]=m[r][0]*p[0]+m[r][1]*p[1]+m[r][2]*p[2]+m[r][3];
        if(!(clip[3]>1e-6f))continue;
        const float x=(clip[0]/clip[3]*0.5f+0.5f)*width,y=(0.5f-clip[1]/clip[3]*0.5f)*height+offset;
        if(!std::isfinite(x)||!std::isfinite(y))continue;
        l=std::fmin(l,x);rgt=std::fmax(rgt,x);t=std::fmin(t,y);b=std::fmax(b,y);++used;
    }
    if(!used||rgt-l<width*0.02f||b-t<height*0.02f)return false;
    out={l/width,t/display_height,rgt/width,b/display_height};
    return true;
}

// The same shader and colours also mark a player's chosen answer on the score
// panels (about 120 of 1280 px wide); the answer boards are about 280 px.
inline constexpr float min_board_width=0.15f;
inline bool is_answer_board(const Box& box){return box.right-box.left>=min_board_width;}

// Runtime (answer_hints.cpp): install the draw observer once; the overlay
// reads the boards seen within the last few frames.
void install();
std::array<std::optional<Box>,board_count> visible_boards(uint64_t max_age_ms=150);
}
