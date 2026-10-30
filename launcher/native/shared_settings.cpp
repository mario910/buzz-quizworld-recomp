#include "core.h"
#include "pc_settings_file.h"
#include <stdexcept>

namespace buzz_launcher {
namespace {
std::optional<std::string> contents(const fs::path& file,size_t limit) {
    if(!fs::exists(file))return {};
    return read_text(file,limit);
}
std::optional<std::string> digest(const std::optional<std::string>& text) {
    return text?std::optional<std::string>(sha256_text(*text)):std::nullopt;
}
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void import_input(Settings& s,const buzz_input::Config& c) {
    s.places=int(c.players);s.pulse_ms=int(c.pulse_ms);
    s.gamepads=c.gamepads;s.background_gamepads=c.background_gamepads;
    for(unsigned p=0;p<8;++p)for(unsigned b=0;b<5;++b)s.players[p].keys[b]=key_label(c.keys[p][b]);
    for(unsigned slot=0;slot<4;++slot) {
        s.pads[slot].player=c.pad_player[slot];
        for(unsigned b=0;b<5;++b) {
            const char* label=buzz_input::pad_button_name(c.pad_buttons[slot][b]);
            check(label!=nullptr,"Unknown saved controller button");s.pads[slot].buttons[b]=label;
        }
    }
}
}
Settings load_shared_settings(const fs::path& root,const Settings& fallback) {
    auto s=fallback;
    auto pc=contents(root/"runtime/pc-settings.ini",4096);
    auto input=contents(root/"runtime/input-settings.ini",65536);
    std::string error;
    if(pc) {
        check(buzz_pc::parse(*pc,s.pc,error),error);
        s.offline=s.pc.offline;s.pc_configured=true;
    }else {
        s.pc=buzz_pc::Preferences{};s.pc.fps_limit=0;s.pc.offline=s.offline;s.pc_configured=false;
    }
    if(input) {
        auto c=input_config(s);check(buzz_input::parse_config(*input,c,error),error);
        import_input(s,c);
    }
    s.shared={fs::weakly_canonical(root),digest(pc),digest(input)};
    validate_settings(s);return s;
}
void verify_shared_settings(const fs::path& root,const Settings& s) {
    const auto pc=root/"runtime/pc-settings.ini",input=root/"runtime/input-settings.ini";
    auto pc_hash=digest(contents(pc,4096)),input_hash=digest(contents(input,65536));
    if(s.shared.root.empty())check(!pc_hash&&!input_hash,"Load the build's existing settings before saving");
    else check(s.shared.root==fs::weakly_canonical(root)&&pc_hash==s.shared.pc_hash&&input_hash==s.shared.input_hash,
               "Settings changed on disk or belong to another build. Reload saved settings before continuing.");
}
void save_shared_settings(const fs::path& root,Settings& s) {
    validate_settings(s);
    check(!fs::is_symlink(root/"runtime/pc-settings.ini")&&!fs::is_symlink(root/"runtime/input-settings.ini"),
          "Linked preferences are read-only; edit their target explicitly");
    auto preferences=s.pc;preferences.offline=s.offline;
    const auto pc_text=buzz_pc::serialize(preferences),input_text=input_ini(s);
    // Validate both files and their revisions before changing either. Each
    // replacement is atomic. An I/O failure can still leave a partial pair;
    // report it explicitly and preserve revisions for files already replaced.
    verify_shared_settings(root,s);
    s.shared.root=fs::weakly_canonical(root);
    try {
        buzz_pc_write_file_atomic(root/"runtime/pc-settings.ini",pc_text);
        s.shared.pc_hash=sha256_text(pc_text);s.pc=preferences;s.pc_configured=true;
        buzz_pc_write_file_atomic(root/"runtime/input-settings.ini",input_text);
        s.shared.input_hash=sha256_text(input_text);
    }catch(const std::exception& e){throw std::runtime_error(std::string("Settings save did not finish; some files may have been saved. Reload before continuing: ")+e.what());}
}
}
