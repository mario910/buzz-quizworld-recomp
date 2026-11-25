#include "../port/buzzer_input.h"
#include <iostream>
#include <iterator>
int main(){
    auto config=buzz_input::defaults();std::string error;
    const std::string text{std::istreambuf_iterator<char>(std::cin),std::istreambuf_iterator<char>()};
    if(!buzz_input::parse_config(text,config,error)){std::cerr<<error;return 1;}
    std::cout<<"{\"players\":"<<config.players<<",\"keys\":[";
    for(unsigned p=0;p<8;++p){if(p)std::cout<<',';std::cout<<'[';
        for(unsigned b=0;b<5;++b){if(b)std::cout<<',';std::cout<<config.keys[p][b];}std::cout<<']';}
    std::cout<<"],\"pad_buttons\":["; // the one layout every gamepad shares
    for(unsigned b=0;b<5;++b){if(b)std::cout<<',';std::cout<<config.pad_buttons[b];}
    std::cout<<"]}\n";
}
