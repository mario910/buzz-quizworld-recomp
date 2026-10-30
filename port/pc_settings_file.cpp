#include "pc_settings_file.h"
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
void buzz_pc_write_file_atomic(const std::filesystem::path& file,const std::string& text) {
    namespace fs=std::filesystem;
    if(fs::is_symlink(file))throw std::runtime_error("Linked settings are read-only; edit their target explicitly");
    fs::create_directories(file.parent_path());std::random_device random;
    auto staging=file.parent_path()/(".pc-write-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(random()));
    if(!fs::create_directory(staging))throw std::runtime_error("Could not create settings staging directory");
    auto temporary=staging/"new";
    try {
        {std::ofstream out(temporary,std::ios::binary);out.write(text.data(),std::streamsize(text.size()));out.flush();if(!out)throw std::runtime_error("Could not save settings");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not replace settings");
#else
        fs::rename(temporary,file);
#endif
        fs::remove(staging);
    }catch(...){std::error_code error;fs::remove(temporary,error);fs::remove(staging,error);throw;}
}
