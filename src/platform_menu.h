#pragma once

// Included by the Mod Platform build. Gameplay hooks use the public API.
namespace wb_menu {
constexpr uint32_t find_property_rva = 0x200C1D0;
constexpr uint32_t hud_buttons_slot = 0x4566B28;
constexpr uint32_t native_hud_buttons = 0x14981A0;
constexpr uint32_t destroy_ref_rva = 0x31EB20;
struct MenuLayout {
    uint32_t api, stamp, image_size, row_set, notify, rows, in_mods, setter_check;
    const char* setter_bytes;
    const char* notify_bytes;
};
constexpr MenuLayout api14{14,0x47A22C2D,0x24C000,0xC0B40,0xA1400,0x2204A8,0x220A98,0x2E,
    "\x48\x8b\x1d\x33\xf9\x15\x00","\x55\x56\x57\x53\x48\x81\xec\xb8"};
constexpr MenuLayout api15{15,0x6AEE7575,0x253000,0xCD1E0,0xAA0F0,0x229FF8,0x22A5E8,0x2C,
    "\x48\x8b\x1d\xe5\xcd\x15\x00","\x55\x56\x57\x53\x48\x81\xec\xc8"};
constexpr MenuLayout api16{16,0x6AEF5530,0x268000,0xDD6A0,0xBA810,0x23E210,0x23E860,0x2C,
    "\x48\x8b\x1d\x3d\x0b\x16\x00","\x55\x56\x57\x53\x48\x81\xec\xc8"};
constexpr int accept_button = 0xF4240, back_button = 0xF423F;

using FindProperty = byte* (*)(byte*, byte*);
using Buttons = bool (*)(void*, int, bool);
using SetRow = void (*)(void*, void*, byte*);
using GetRow = void** (*)(void**, void*, void*);
using Notify = void (*)(void*, void*);
using DestroyRef = void (*)(void*);
using EscapeXml = byte* (*)(byte*,const byte*);
constexpr char setting_tag[] = " [WB option]";
constexpr char owner_suffix[] = " (WreckingBall)";

const byte* platform{};
const MenuLayout* layout=&api14;
FindProperty original_find{};
Buttons original_buttons{};
SetRow original_set{};
GetRow original_get{};
Notify notify{};
DestroyRef destroy_ref{};
EscapeXml original_escape{};

byte* escape_setting_text(byte* out,const byte* input) {
    const bool on_heap=(input[0]&1)!=0;
    const auto size=on_heap?*reinterpret_cast<const size_t*>(input+8):size_t(input[0]>>1);
    const auto data=on_heap?*reinterpret_cast<const char* const*>(input+16):reinterpret_cast<const char*>(input+1);
    auto end=size;
    constexpr size_t suffix_size=sizeof owner_suffix-1,tag_size=sizeof setting_tag-1;
    if (end>=suffix_size && memcmp(data+end-suffix_size,owner_suffix,suffix_size)==0) end-=suffix_size;
    if (end<tag_size || memcmp(data+end-tag_size,setting_tag,tag_size)!=0) return original_escape(out,input);
    // The platform escapes this string by length. Give the encoder a
    // shortened view and leave its original string and owner intact.
    alignas(8) byte trimmed[24];
    memcpy(trimmed,input,sizeof trimmed);
    end-=tag_size;
    if (on_heap) *reinterpret_cast<size_t*>(trimmed+8)=end;
    else trimmed[0]=static_cast<byte>(end*2);
    return original_escape(out,trimmed);
}

struct Preview {
    void* descriptor{};
    byte* model{};
    byte* value{};
};
thread_local std::array<Preview,13> pending;
thread_local void* menu_page{};
thread_local bool refreshing{};

template<class T>
T read(const void* address) {
    T value{};
    if (readable(address, sizeof value)) std::memcpy(&value, address, sizeof value);
    return value;
}
bool in_mods() {
    return platform && platform[layout->in_mods] == 1;
}
uint32_t& references(byte* value) {
    return *reinterpret_cast<uint32_t*>(value + 0x10);
}
void release(byte* value) {
    if (value && --references(value) == 0) destroy_ref(value);
}
void finish_previews(bool accept, bool same_page) {
    refreshing = true;
    for (auto& entry : pending) {
        const Preview previous = entry;
        entry = {};
        if (!previous.value) continue;
        if (same_page) {
            if (accept) original_set(previous.descriptor, previous.model, previous.value);
            notify(previous.model, previous.descriptor);
        }
        release(previous.value);
        release(previous.model);
    }
    refreshing = false;
}
void** preview_value(void** out, void* descriptor, void* model) {
    if (!refreshing && in_mods() && menu_page) {
        for (const auto& entry : pending) {
            if (entry.descriptor != descriptor) continue;
            ++references(entry.value);
            *out = entry.value;
            return out;
        }
    }
    return original_get(out, descriptor, model);
}
void preview_setting(void* descriptor, void* model, byte* value) {
    if (refreshing) return;
    if (!in_mods()) {
        original_set(descriptor, model, value);
        return;
    }
    if (!menu_page || !readable(model, 0x30) || !readable(value, 0x2C)) return;
    Preview* slot = nullptr;
    for (auto& entry : pending) {
        if (entry.descriptor == descriptor) { slot = &entry; break; }
        if (!entry.value && !slot) slot = &entry;
    }
    if (!slot) return;
    void* accepted_object{};
    original_get(&accepted_object, descriptor, model);
    auto accepted = static_cast<byte*>(accepted_object);
    const bool changed = !accepted || std::memcmp(accepted + 0x28, value + 0x28, 4) != 0;
    release(accepted);
    const Preview previous = *slot;
    *slot = {};
    if (changed) {
        auto source = static_cast<byte*>(model);
        ++references(source);
        ++references(value);
        *slot = {descriptor, source, value};
    }
    release(previous.value);
    release(previous.model);
}
bool page_buttons(void* page, int button, bool pressed) {
    const bool active = in_mods();
    if (!active || menu_page != page) finish_previews(false, false);
    menu_page = active ? page : nullptr;
    if (active && pressed) {
        if (button == accept_button &&
            std::any_of(pending.begin(),pending.end(),[](const Preview& entry) { return entry.value; })) {
            finish_previews(true, true);
            return true;
        }
        if (button == back_button) finish_previews(false, true);
    }
    return original_buttons(page, button, pressed);
}

// The loader stores rows in a libc++ deque, with 19 0xD0-byte rows per block.
// Read only the indexed row and verify its descriptor and owner before
// intercepting it. The platform's own setters and other mods keep theirs.
bool owns_property(byte* descriptor, byte* name) {
    const auto text = name ? read<const wchar_t*>(name + 0x28) : nullptr;
    if (!readable(text, 8 * sizeof(wchar_t)) || std::wmemcmp(text, L"FH3Mod_", 7) != 0) return false;
    size_t index = 0, digits = 0;
    for (auto p = text + 7; readable(p, sizeof *p) && *p >= L'0' && *p <= L'9'; ++p) {
        if (++digits > 4) return false;
        index = index * 10 + (*p - L'0');
        if (!readable(p + 1, sizeof *p)) return false;
        if (p[1] == 0) {
            const auto blocks = read<const byte* const*>(platform + layout->rows);
            const auto start = read<size_t>(platform + layout->rows + 0x18);
            const auto count = read<size_t>(platform + layout->rows + 0x20);
            if (!blocks || index >= count || start > 10000) return false;
            const auto block = read<const byte*>(blocks + (start + index) / 19);
            if (!block) return false;
            const auto row = block + (start + index) % 19 * 0xD0;
            return read<FH3Mod*>(row) == g_mod && read<byte*>(row + 0x88) == descriptor;
        }
    }
    return false;
}
byte* find_property(byte* type, byte* name) {
    auto descriptor = original_find(type, name);
    if (descriptor && in_mods() && read<SetRow>(descriptor + 0x30) == original_set && owns_property(descriptor, name)) {
        // These rows share the platform's getter. Bindings that are recreated
        // when scrolling must read the staged value until A or Back.
        const auto getter = read<GetRow>(descriptor + 0x28);
        if (!getter || (original_get && getter != original_get)) return descriptor;
        original_get = getter;
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(descriptor + 0x28),
                                          reinterpret_cast<void*>(preview_value),
                                          reinterpret_cast<void*>(original_get));
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(descriptor + 0x30),
                                          reinterpret_cast<void*>(preview_setting),
                                          reinterpret_cast<void*>(original_set));
    }
    return descriptor;
}

const byte* supported_platform() {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(g_platform->register_mod), &module)) return nullptr;
    const auto base = reinterpret_cast<const byte*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x1000) return nullptr;
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto stamp = pe->FileHeader.TimeDateStamp;
    const auto api=g_platform->api_version;
    const auto& candidate=api==16?api16:api==15?api15:api14;
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->OptionalHeader.SizeOfImage != candidate.image_size ||
        api!=candidate.api ||
        (stamp!=candidate.stamp && !(candidate.api==14 && stamp==0xD8851FC5))) return nullptr;
    // Check an instruction inside the setter so a chained entry hook from
    // another mod does not invalidate the match.
    if (std::memcmp(base+candidate.row_set+candidate.setter_check,candidate.setter_bytes,7)!=0 ||
        std::memcmp(base+candidate.notify,candidate.notify_bytes,8)!=0) return nullptr;
    layout=&candidate;
    return base;
}
bool hide_setting_owner() {
    const auto base=supported_platform();
    if (!base) return false;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto section=IMAGE_FIRST_SECTION(pe);
    if (memcmp(section->Name,".text",6)!=0 || section->VirtualAddress>=layout->image_size ||
        section->Misc.VirtualSize>layout->image_size-section->VirtualAddress) return false;
    const auto start=base+section->VirtualAddress,end=start+section->Misc.VirtualSize;
    constexpr char body[]="\x48\x89\xce\x0f\x57\xc0\x0f\x11\x01\x48\xc7\x41\x10\x00\x00\x00\x00"
                          "\x0f\xb6\x3a\x89\xf8\x48\x8d\x5a\x01\xd1\xef\xa8\x01\x48\x0f\x45\x5a\x10\x48\x0f\x45\x7a\x08";
    const auto match=std::search(start,end,reinterpret_cast<const byte*>(body),
        reinterpret_cast<const byte*>(body)+sizeof body-1);
    if (match==end || match-start<22 ||
        std::search(match+1,end,reinterpret_cast<const byte*>(body),reinterpret_cast<const byte*>(body)+sizeof body-1)!=end) return false;
    constexpr char prologue14[]="\x55\x41\x57\x41\x56\x41\x55\x41\x54\x56\x57\x53\x48\x83\xec\x28\x48\x8d\x6c\x24\x20";
    constexpr char prologue16[]="\x41\x57\x41\x56\x41\x55\x41\x54\x56\x57\x55\x53\x48\x83\xec\x28";
    auto target=match-(sizeof prologue16-1);
    if (memcmp(target,prologue16,sizeof prologue16-1)!=0) {
        target=match-(sizeof prologue14-1);
        if (memcmp(target,prologue14,sizeof prologue14-1)!=0) return false;
    }
    const auto hook=g_platform->hook_function(g_mod,"WreckingBall option description",const_cast<byte*>(target),
        reinterpret_cast<void*>(escape_setting_text),reinterpret_cast<void**>(&original_escape));
    return hook && g_platform->enable_hooks(g_mod,hook);
}
bool install(byte* image) {
    platform = supported_platform();
    if (!platform || !g_platform->hook_pointer ||
        std::memcmp(image + native_hud_buttons, "\x48\x89\x5c\x24\x08\x48\x89\x74", 8) != 0) return false;
    destroy_ref = reinterpret_cast<DestroyRef>(image + destroy_ref_rva);
    original_set = reinterpret_cast<SetRow>(platform + layout->row_set);
    notify = reinterpret_cast<Notify>(platform + layout->notify);
    auto buttons_slot = reinterpret_cast<void**>(image + hud_buttons_slot);
    // Let the platform install its native Mods page before joining its hooks.
    for (unsigned tries = 0; tries < 200 && read<void*>(buttons_slot) == image + native_hud_buttons; ++tries) Sleep(25);
    if (read<void*>(buttons_slot) == image + native_hud_buttons) return false;
    auto buttons = g_platform->hook_pointer(g_mod, "WreckingBall settings accept", buttons_slot,
                                           reinterpret_cast<void*>(page_buttons), reinterpret_cast<void**>(&original_buttons));
    auto property = g_platform->hook_function(g_mod, "WreckingBall settings preview", image + find_property_rva,
                                              reinterpret_cast<void*>(find_property), reinterpret_cast<void**>(&original_find));
    if (!buttons || !property || !g_platform->enable_hooks(g_mod, buttons)) return false;
    if (g_platform->enable_hooks(g_mod, property)) return true;
    g_platform->disable_hooks(g_mod, buttons);
    return false;
}
}
