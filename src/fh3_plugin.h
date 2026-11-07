#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstdint>

// ExtraOptions 0.4.4 FH3, API 1. Verified against the supplied loader.
// This is distinct from both fh2_plugin.h and FH2 Mod Platform's fh2mod.h.
struct FH3PluginContext {
    uint32_t size, api_version;
    void* game_image;
    uint32_t game_image_size, reserved;
    HMODULE plugin_module;
    const wchar_t* mod_directory;
    const wchar_t* plugin_path;
    const char* extra_options_version;
    void (*log_utf8)(const char*);
    BOOL (*write_patch)(uint32_t, const void*, const void*, uint32_t);
    void* (*hook_call)(uint32_t, void*);
    void* (*hook_pointer)(uint32_t, void*);
    void* (*local_car)();
};
static_assert(sizeof(FH3PluginContext) == 96);
static_assert(offsetof(FH3PluginContext, game_image) == 8);
static_assert(offsetof(FH3PluginContext, mod_directory) == 32);
static_assert(offsetof(FH3PluginContext, log_utf8) == 56);
struct FH3PluginFrame { uint32_t size; float delta_seconds; void* car; };
static_assert(sizeof(FH3PluginFrame) == 16);
