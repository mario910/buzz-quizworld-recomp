/* Offline diagnostic using the renderer's own bytecode parser. */
#include "../ps3recomp/libs/video/rsx_fp_decompiler.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){
    if(argc!=2)return 2;
    DIR* dir=opendir(argv[1]);if(!dir)return 2;
    struct dirent* entry;unsigned count=0,mask=0;
    while((entry=readdir(dir))){
        if(!strstr(entry->d_name,"_fp.bin"))continue;
        char path[1024];snprintf(path,sizeof path,"%s/%s",argv[1],entry->d_name);
        FILE*f=fopen(path,"rb");if(!f)continue;
        unsigned char b[16384];unsigned n=fread(b,1,sizeof b,f);fclose(f);
        unsigned units=rsx_fp_texture_units(b,n);mask|=units;++count;
        if(units&~15u)printf("%s units=%04x\n",entry->d_name,units);
    }
    closedir(dir);printf("programs=%u combined_units=%04x\n",count,mask);return 0;
}
