// Exercise the callbacks that the platform receives, using a private INI.
#define WB_MOD_PLATFORM
#include "../src/wreckingball_fh3.cpp"
#include <cassert>
#include <filesystem>
#include <map>

namespace {
std::map<std::string, FH3Setting> rows;
int commits{}, refreshes{};
HANDLE worker_done{};

void check_confirmation() {
    using namespace wb_menu;
    std::vector<byte> module(api14.in_mods + 1);
    platform = module.data();
    module[api14.in_mods] = 1;
    destroy_ref = [](void*) { std::abort(); };
    original_buttons = [](void*, int, bool) { return false; };
    original_set = [](void* descriptor, void*, byte* value) {
        ++commits;
        memcpy(static_cast<byte*>(descriptor) + 0x28, value + 0x28, 4);
    };
    notify = [](void*, void*) { ++refreshes; };
    original_get = [](void** out, void* descriptor, void*) {
        auto accepted=static_cast<byte*>(descriptor);
        ++references(accepted);
        *out = accepted;
        return out;
    };
    alignas(8) byte row1[0x30]{},row2[0x30]{},toggle[0x30]{},slider[0x30]{},ui_model[0x30]{};
    for (auto value:{row1,row2,toggle,slider,ui_model}) references(value)=1;
    *reinterpret_cast<int*>(toggle+0x28)=1;
    *reinterpret_cast<float*>(slider+0x28)=0.32f;
    const auto shown=[&](byte* row) {
        void* out{};
        preview_value(&out,row,ui_model);
        auto value=static_cast<byte*>(out);
        release(value);
        return value;
    };
    auto page = &module;
    page_buttons(page, 25, true);
    preview_setting(row1,ui_model,toggle);
    page_buttons(page, 24, true); // navigation must preserve previous rows
    preview_setting(row2,ui_model,slider);
    assert(commits==0 && refreshes==0 && references(ui_model)==3);
    assert(shown(row1)==toggle && shown(row2)==slider);
    page_buttons(page, back_button, true);
    assert(commits==0 && refreshes==2 && references(ui_model)==1);
    assert(shown(row1)==row1 && shown(row2)==row2);
    preview_setting(row1,ui_model,toggle);
    preview_setting(row2,ui_model,slider);
    page_buttons(page, 24, true);
    assert(page_buttons(page, accept_button, true));
    assert(commits==2 && refreshes==4 && references(ui_model)==1);
    assert(*reinterpret_cast<int*>(shown(row1)+0x28)==1);
    assert(*reinterpret_cast<float*>(shown(row2)+0x28)==0.32f);
    *reinterpret_cast<int*>(toggle+0x28)=0;
    preview_setting(row1,ui_model,toggle);
    preview_setting(row1,ui_model,row1); // restored its accepted value
    assert(!page_buttons(page,accept_button,true));
    assert(commits==2 && shown(row1)==row1 && references(toggle)==1);
    preview_setting(row1,ui_model,toggle);
    int another_page{};
    page_buttons(&another_page,25,true);
    assert(commits==2 && shown(row1)==row1 && references(ui_model)==1);
    platform = nullptr;
}
}

int main() {
    auto directory = std::filesystem::absolute("settings-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory / "fh3-mods");
    auto ini = directory / "fh3-mods" / "WreckingBall.ini";
    assert(WritePrivateProfileStringW(L"WreckingBall", L"Enabled", L"0", ini.c_str()));
    assert(WritePrivateProfileStringW(L"WreckingBall", L"Fences", L"0", ini.c_str()));
    assert(WritePrivateProfileStringW(L"WreckingBall", L"InjectBushes", L"1", ini.c_str()));
    assert(WritePrivateProfileStringW(L"WreckingBall", L"BushSpeedLoss", L"-1", ini.c_str()));

    FH3ModApi api{};
    api.size = sizeof(api);
    api.api_version = FH3MOD_API_VERSION;
    api.azimuth_dir = directory.c_str();
    api.register_mod = [](const FH3ModInfo*) { return reinterpret_cast<FH3Mod*>(1); };
    worker_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(worker_done);
    api.log = [](FH3Mod*, const char* message) {
        if (strstr(message, "mapped game image")) SetEvent(worker_done);
    };
    api.hook_function = [](FH3Mod*, const char*, void*, void*, void**) -> FH3Hook* { return nullptr; };
    api.enable_hooks = api.disable_hooks = [](FH3Mod*, FH3Hook*) { return 0; };
    api.wait_for_code = [](uint32_t) -> void* { return nullptr; };
    api.add_setting = [](FH3Mod*, const FH3Setting* row) { return rows.emplace(row->key, *row).second ? 1 : 0; };
    assert(fh3mod_init(&api) == 0);
    assert(WaitForSingleObject(worker_done, 5000) == WAIT_OBJECT_0);
    CloseHandle(worker_done);
    assert(rows.size() == 13 && rows.count("Guardrails") && !rows.count("Poles") && !rows.count("Bins") && !rows.count("Clutter"));
    const auto& benches=rows.at("ConcreteBenches");
    assert(benches.value==1 && !benches.needs_restart);
    benches.on_change(benches.user,benches.key,0);
    assert(!(g_scenery_mask.load()&wb::kind_bit(wb::Kind::Furniture)));
    assert(!rows.count("InjectBushes") && !rows.count("BushSpeedLoss"));
    const auto& enabled = rows.at("Enabled");
    assert(enabled.value == 0 && !enabled.needs_restart && !g_scenery_mask.load());
    enabled.on_change(enabled.user, enabled.key, 1);
    assert(ini_text(ini.wstring(), L"Enabled", L"") == "1" && g_enabled.load());
    const auto& fences = rows.at("Fences");
    assert(fences.value == 0 && !fences.needs_restart && !(g_scenery_mask.load()&wb::kind_bit(wb::Kind::Fence)));
    fences.on_change(fences.user, fences.key, 1);
    assert(ini_text(ini.wstring(), L"Fences", L"") == "1");
    assert(g_scenery_mask.load()&wb::kind_bit(wb::Kind::Fence));

    const auto& debris = rows.at("DebrisCleanup");
    assert(debris.value == 1 && !debris.needs_restart);
    debris.on_change(debris.user, debris.key, 0);
    assert(!g_debris_cleanup && ini_text(ini.wstring(), L"DebrisCleanup", L"") == "0");

    const auto& maximum = rows.at("SpeedLossMax");
    maximum.on_slide(maximum.user, maximum.key, 0.32f);
    assert(current_slowdown().maximum == 0.32f);
    load_settings(ini.wstring());
    assert(g_slowdown.maximum == 0.32f && !g_debris_cleanup);
    assert(g_rules.categories[static_cast<size_t>(wb::Kind::Fence)]);
    check_confirmation();
    fclose(g_log);
    std::filesystem::remove(ini);
    std::filesystem::remove(directory / "fh3-mods" / "WreckingBall.log");
    std::filesystem::remove(directory / "fh3-mods");
    std::filesystem::remove(directory);
    puts("Platform settings registration, live updates and INI persistence passed");
}
