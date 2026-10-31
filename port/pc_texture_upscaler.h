#pragma once
#include <filesystem>
// Automatic texture upscaling. A background thread feeds each new texture the
// renderer dumps (textures/dump) through Real-ESRGAN into textures/replace/auto,
// and signals the renderer to pick the result up the next time the game uploads
// that texture. Upscales persist, so later runs start with them.
namespace buzz_pc {
// Where the game looks for the upscaler (realesrgan-ncnn-vulkan, with its
// models/ folder beside it); BUZZ_TEXTURE_UPSCALER overrides it.
inline std::filesystem::path texture_upscaler_executable(const std::filesystem::path& textures) {
#ifdef _WIN32
    return textures/"upscaler"/"realesrgan-ncnn-vulkan.exe";
#else
    return textures/"upscaler"/"realesrgan-ncnn-vulkan";
#endif
}
// Starts the worker; false, with a log line, when the upscaler is missing.
bool start_texture_upscaler(const std::filesystem::path& textures);
}
