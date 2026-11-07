// Build this as azimuth.exe in a private fixture with an installed FH3 loader.
// Map the game image for signature checks and trampolines without starting it.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string platform_log() {
    std::ifstream file("fh3-mods/platform.log");
    return {std::istreambuf_iterator<char>(file), {}};
}

unsigned char* map_game() {
    std::ifstream file("game/Mount/forza_durango_release_final.exe", std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0x1000) return nullptr;
    std::vector<char> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(bytes.data(), bytes.size())) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 0x800) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 0x5B16ECCA ||
        nt->OptionalHeader.SizeOfImage != 0x5495000) return nullptr;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(nt->OptionalHeader.ImageBase),
        nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!image) return nullptr;
    std::memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        std::memcpy(image + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData,
                    section[i].SizeOfRawData);
    }
    return image;
}

bool check_alias(unsigned char* image) {
    alignas(16) unsigned char definition[0x220]{};
    auto* vtable = image + 0x4682390;
    std::memcpy(definition, &vtable, sizeof(vtable));
    int model_index = 123;
    std::memcpy(definition + 0xC0, &model_index, sizeof(model_index));
    constexpr char name[] = "tracks/australia/tree_gorge_ghostgum_04_smashable.rmb";
    std::memcpy(definition + 0x11C, name, sizeof(name));
    unsigned char* roots[] = {definition};
    struct Table { void* vtable; unsigned char** roots; uint64_t count; uint64_t reserved; };
    Table table{image + 0x4682340, roots, 1, 0};
    auto lookup = reinterpret_cast<void* (*)(void*, uint32_t)>(image + 0x1D43CB0);
    auto model = reinterpret_cast<int (*)(void*, uint32_t)>(image + 0x1D451D0);
    return lookup(&table, 0xFFFF0000) == definition && model(&table, 0xFFFFF004) == model_index;
}

bool check_rows(HMODULE loader) {
    // Verify the row ownership layout against the actual proxy, not a stub.
    const auto base = reinterpret_cast<const unsigned char*>(loader);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto stamp = nt->FileHeader.TimeDateStamp;
    const auto offset = stamp == 0x6AEF5530 ? 0x23E210 : stamp == 0x6AEE7575 ? 0x229FF8 : 0x2204A8;
    const auto blocks = *reinterpret_cast<const unsigned char* const* const*>(base + offset);
    const auto first = *reinterpret_cast<const size_t*>(base + offset + 0x18);
    const auto count = *reinterpret_cast<const size_t*>(base + offset + 0x20);
    if (!blocks || first > 10000 || count < 13 || count > 256) return false;
    const auto row = [&](size_t index) {
        index += first;
        return blocks[index / 19] + (index % 19) * 0xD0;
    };
    const auto owner = *reinterpret_cast<void* const*>(row(0));
    if (!owner) return false;
    for (size_t i = 0; i < 13; ++i) {
        const auto entry = row(i);
        if (*reinterpret_cast<void* const*>(entry) != owner || entry[0x90] != (i >= 9)) return false;
    }
    return true;
}
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc < 2) return 1;
    auto* image = map_game();
    if (!image) return 2;
    for (int i = 1; i < argc; ++i) {
        if (!LoadLibraryW(std::filesystem::absolute(argv[i]).c_str())) return 3;
    }
    for (unsigned attempt = 0; attempt < 400; ++attempt) {
        const auto log = platform_log();
        if (log.find("WreckingBall FH3 hooks are ready.") != std::string::npos &&
            log.find("WreckingBall settings preview and A confirmation are ready.") != std::string::npos) {
            if (!check_alias(image) || !check_rows(GetModuleHandleW(argv[1]))) return 4;
            std::cout << "PASS: the real platform enabled WreckingBall's gameplay and menu hooks.\n";
            return 0;
        }
        if (log.find("WreckingBall FH3 startup refused") != std::string::npos ||
            log.find("WreckingBall FH3 hook setup failed") != std::string::npos) break;
        Sleep(50);
    }
    std::cerr << platform_log();
    return 5;
}
