#pragma once
#include "buzzer_input.h"
#include "pc_settings.h"
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace buzz_launcher {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Environment = std::map<std::string, std::string>;
inline constexpr const char* elf102 = "9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241";
inline constexpr const char* elf100 = "6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8";
inline constexpr const char* elfUS = "fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d";
inline constexpr const char* button_names[] = {"Red", "Yellow", "Green", "Orange", "Blue"};
inline constexpr const char* pad_names[] = {"START","BACK","A","B","X","Y","LB","RB","UP","DOWN","LEFT","RIGHT","LS","RS"};
struct Player { std::string name; std::array<std::string,5> keys; };
struct Pad { int player = -1; std::array<std::string,5> buttons = {"RB","LEFT","DOWN","RIGHT","UP"}; };
struct SharedRevision {
    // Runtime-only revision metadata, never serialized into legacy JSON.
    fs::path root;
    std::optional<std::string> pc_hash,input_hash;
};
struct Settings {
    std::map<std::string,std::string> paths = {{"game_root",""},{"dev_flash",""},{"eboot_elf",""}};
    int places = 4, pulse_ms = 250;
    bool offline = true, hide_store = true, gamepads = true, background = true;
    bool background_gamepads = false;
    buzz_pc::Preferences pc;
    bool pc_configured = false;
    SharedRevision shared;
    std::array<Player,8> players;
    std::array<Pad,4> pads;
    Settings();
};
struct Profile { std::string family, version; };
struct Build { fs::path root, executable; Json manifest; Profile profile; };
struct LaunchPlan {
    fs::path executable, elf, cwd, stdout_path, stderr_path, session;
    Environment environment;
};
std::string utf8(const fs::path& p);
fs::path path(const std::string& s);
fs::path resolve(const fs::path& root, const std::string& value);
std::string read_text(const fs::path&, std::size_t limit = 65536);
void atomic_text(const fs::path&, const std::string&);
std::string sha256(const fs::path&);
std::string sha256_text(const std::string&);
Json settings_json(const Settings&);
Settings settings_from_json(const Json&);
Settings load_settings(const fs::path& root);
// Per-build INIs are authoritative for gameplay settings; JSON remains a
// backward-compatible path/label store. Reads never mutate either format.
Settings load_shared_settings(const fs::path& build_root,const Settings& fallback);
void save_shared_settings(const fs::path& build_root,Settings&);
void verify_shared_settings(const fs::path& build_root,const Settings&);
void validate_settings(const Settings&);
std::string input_ini(const Settings&);
unsigned key_code(const std::string&);
std::string key_label(unsigned);
buzz_input::Config input_config(const Settings&);
std::vector<std::string> warnings(const Settings&);
std::string title_id(const std::string& sfo);
Profile identify(const std::string& title, const std::string& digest);
void validate_prepared_content(const Profile&,const fs::path& build_root);
// Scans only this project's managed preparation sessions, or explicit portable
// game/build directories. A valid manifest is mandatory; no legacy bypass.
Build validate_build(const Settings&, const fs::path& catalog);
Environment runtime_environment(const Settings&, const Build&, const fs::path& session,
                                Environment inherited);
LaunchPlan plan_launch(const Settings&, const Build&, Environment inherited);
Environment current_environment();
bool executable_running(const fs::path&);
class Child {
    void* process_ = nullptr;
    void* out_ = nullptr;
    void* err_ = nullptr;
public:
    Child() = default;
    ~Child(); // Never kills a game when the launcher closes.
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    void start(const LaunchPlan&);
    bool running(int* exit_code = nullptr);
    void stop_owned(); // Used only by bounded automated integration, not UI quit.
};
int run_ui(const fs::path& project, bool smoke, const fs::path& capture, int smoke_tab = 0);
}
