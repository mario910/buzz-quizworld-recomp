// Answer-board detection and projection, with constants and vertex extents
// taken from a captured question frame (rsxcap, disc 1.02, op1070..op1076).
#include "../port/answer_hints.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint32_t bits(float f){uint32_t u;std::memcpy(&u,&f,sizeof u);return u;}
std::vector<uint32_t> board_constants(float r,float g,float b,float fade=0){
    const float k[5][4]={{-0.5f,0,0,0},{r,g,b,1},{r,g,b,1},{r,g,b,1},{12,1,0.300000012f,fade}};
    std::vector<uint32_t> out;for(auto& row:k)for(float v:row)out.push_back(bits(v));return out;
}
}
int main(){try {
    using namespace buzz_hints;
    // The four board colours exactly as the game's fragment constants carry them.
    check(classify(board_constants(0.0980392098f,0.396078438f,0.99999994f).data(),5)==blue,"Blue board");
    check(classify(board_constants(0.99999994f,0.517646968f,0,0.1f).data(),5)==orange,"Orange board (fade 0.1)");
    check(classify(board_constants(0.282352895f,0.99999994f,0).data(),5)==green,"Green board");
    check(classify(board_constants(0.999999881f,0.917647004f,0,0.2f).data(),5)==yellow,"Yellow board");
    // Anything else is not a board: other colours, other shaders, short blocks.
    check(classify(board_constants(1,0,0).data(),5)==-1,"Red is not an answer board");
    auto other=board_constants(0.0980392098f,0.396078438f,0.99999994f);other[16]=bits(11);
    check(classify(other.data(),5)==-1,"Different k4 is a different shader");
    auto mixed=board_constants(0.0980392098f,0.396078438f,0.99999994f);mixed[8]=bits(0.5f);
    check(classify(mixed.data(),5)==-1,"k1..k3 must agree");
    check(classify(board_constants(0.0980392098f,0.396078438f,0.99999994f).data(),4)==-1&&classify(nullptr,5)==-1,"Too few constants");

    // The blue board's clip matrix (c56..c59) and its vertex extents.
    std::vector<uint32_t> vs(512*4,0);
    const float rows[4][4]={{2.41421f,0,0,-2.38717f},{0,-1.87606e-07f,4.29194f,-53.1343f},{0,1.0202f,4.45944e-08f,89.8218f},{0,1,4.37114e-08f,107.845f}};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)vs[(56+r)*4+c]=bits(rows[r][c]);
    std::vector<float> vertices; // two attributes per vertex; position in slot 1
    for(float x:{-44.9627f,-23.9231f})for(float y:{15.6122f,23.6460f})for(float z:{16.7342f,23.6447f})
        for(float v:{0.f,0.f,0.f,1.f,x,y,z,1.f})vertices.push_back(v);
    Box box{};
    check(project(vs.data(),512,reinterpret_cast<const uint8_t*>(vertices.data()),8,32,16,1280,704,box),"Blue board projects");
    // 1280x704 target composed 8 px down in 1280x720: the screenshot shows ~70..345 x 225..305.
    const float l=box.left*1280,r=box.right*1280,t=box.top*720,b=box.bottom*720;
    std::printf("blue board on screen: %.1f..%.1f x %.1f..%.1f\n",l,r,t,b);
    check(l>60&&l<75&&r>340&&r<352&&t>215&&t<230&&b>300&&b<315,"Projected box matches the captured screen");
    check(is_answer_board(box),"An answer board is wide enough");
    // A score panel's answer marker (~1000..1120 px of 1280) uses the same shader.
    check(!is_answer_board(Box{1000/1280.f,230/720.f,1120/1280.f,310/720.f}),"Score panel marker is not an answer board");
    check(!project(vs.data(),512,reinterpret_cast<const uint8_t*>(vertices.data()),8,32,-1,1280,704,box),"No position attribute");
    check(!project(vs.data(),512,reinterpret_cast<const uint8_t*>(vertices.data()),0,32,16,1280,704,box),"No vertices");
    std::vector<uint32_t> empty(512*4,0);
    check(!project(empty.data(),512,reinterpret_cast<const uint8_t*>(vertices.data()),8,32,16,1280,704,box),"A zero matrix projects nothing");
    // Board order on screen and on the handset maps to the wire buttons blue..yellow.
    check(wire_button[blue]==4&&wire_button[orange]==3&&wire_button[green]==2&&wire_button[yellow]==1,"Board to buzzer button");
    std::cout<<"PASS answer hints: board colours from fragment constants, captured clip matrix, screen box\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
