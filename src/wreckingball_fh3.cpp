// WreckingBall FH3 port, based on Trinculo54's WreckingBall 1.3.3.
// Copyright (c) WreckingBall contributors. GPL-3.0-or-later.
// The procedural injection and collision filtering approach originates in
// https://github.com/Trinculo54/WreckingBall . See LICENSE and CREDITS.md.
#include "fh3_plugin.h"
#include "coverage.h"
#include "australia_scenery_models.h"
#include "placed_catalog.h"
#include "native_clutter_catalog.h"
#include "rules.h"
#ifdef WB_MOD_PLATFORM
#include "fh3mod.h"
#else
#include "MinHook.h"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <share.h>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
using byte = unsigned char;
constexpr uint32_t IMAGE_SIZE = 0x5495000, TIMESTAMP = 0x5B16ECCA;
constexpr uintptr_t SERIALIZE = 0x232EA30, RESIZE_GROUPS = 0x233FCC0;
constexpr uintptr_t PROCEDURAL_DESTRUCTOR = 0x23195A0;
constexpr uintptr_t DECODE_INSTANCE = 0x2332740;
constexpr uintptr_t DEF_LOOKUP = 0x1D43CB0, DEF_MODEL = 0x1D451D0;
constexpr uintptr_t TEMPLATE_QUERY = 0x1D43E40, DEF_VTABLE = 0x4682390;
constexpr uintptr_t WORLD_TEMPLATE_CALL = 0x1D4480E, TEMPLATE_DESERIALIZE = 0x1D61210;
constexpr uintptr_t BIND_LOOSE_MODEL = 0x415FC0;
constexpr uintptr_t BREAK_PIECES = 0x406E20, OBJECT_DEFINITION_ID = 0x4108B0, RELEASE_OBJECT = 0x41BE60;
constexpr uintptr_t RECORD_BROKEN_KEY = 0x436820, TRACK_MODEL_LOOKUP = 0x1DCD1E2, TRACK_MODEL_UNLOAD = 0x1E75BC4;
constexpr uintptr_t FRACTURE_ROOT_CALL = 0x406E65, FRACTURE_CHILD_CALL = 0x407230;
constexpr uintptr_t FRACTURE_ROOT_RETURN = FRACTURE_ROOT_CALL+13, FRACTURE_CHILD_RETURN = FRACTURE_CHILD_CALL+17;
constexpr uintptr_t ON_COLLISION = 0x417070;
constexpr uintptr_t TABLE_CONSTRUCTOR = 0x1D11A80, TABLE_DESTRUCTOR = 0x1D1B590;
constexpr uintptr_t MODEL_BIND = 0x23235E0, TABLE_VTABLE = 0x4682340;
constexpr uintptr_t ADD_CELL = 0x406350, WORLD_UPDATE = 0x424A90;
constexpr uintptr_t TREE_PROVIDER_VTABLE = 0x4723F70;
constexpr uintptr_t CAR_FROM_BODY = 0x1898B90;
constexpr uintptr_t DYNAMICS_VTABLE = 0x4680F30, TRACKED_DYNAMICS_VTABLE = 0x4682608;
constexpr uintptr_t GET_VELOCITY = 0x1D46C60, SET_VELOCITY = 0x1D69B60;
constexpr size_t DEFINITION_SIZE = 0x220;
constexpr size_t DEFINITION_MASS = 0x9C;
constexpr unsigned MAX_FRAGMENTS = 12;
constexpr size_t MODEL_BATCH_SIZE = 0x70, MODEL_BATCH_NAME = 0x48;
constexpr uint32_t TREE_FIRST = 0xF000, TREE_TIERS = 16;
constexpr uint32_t WOOD_FENCE = 0xF011, WIRE_FENCE = 0xF012, METAL_FENCE = 0xF013;
constexpr uint32_t BARRIER_END = WOOD_FENCE+9;
constexpr wb::Kind barrier_kinds[]{wb::Kind::Fence,wb::Kind::Railing,wb::Kind::Guardrail};
constexpr uint32_t CLUTTER_FIRST = 0xF020, CLUTTER_END = 0xF820;
constexpr char VERSION[] = "1.0.0";
#ifdef WB_MOD_PLATFORM
const FH3ModApi* g_platform = nullptr;
FH3Mod* g_mod = nullptr;
namespace wb_menu { const byte* supported_platform(); }
#else
const FH3PluginContext* g_context = nullptr;
#endif
byte* g_base = nullptr;
FILE* g_log = nullptr;
std::mutex g_log_mutex;
std::atomic<bool> g_ready{false};
wb::Rules g_rules;
std::mutex g_rules_mutex;
std::atomic<wb::SceneryMask> g_scenery_mask{wb::scenery_mask(g_rules)};
std::atomic<bool> g_enabled{true};
std::mutex g_slowdown_mutex;
wb::Slowdown g_slowdown;
bool g_debug = false, g_ground_check = true;
std::atomic<bool> g_debris_cleanup{true};
float g_debris_lifetime = 45, g_debris_distance = 120;
float g_tree_mass = 1.0f, g_break_speed = 1.0f;
float g_big_tree_size=1.6f,g_trunk_radius=1.6f,g_barrier_radius=0.9f;
std::wstring g_directory;

wb::Rules current_rules() {
    std::lock_guard<std::mutex> lock(g_rules_mutex);
    return g_rules;
}
void publish_scenery() {
    g_scenery_mask.store(wb::scenery_mask(g_rules),std::memory_order_release);
    g_enabled.store(g_rules.enabled,std::memory_order_release);
}

void log(const char* fmt, ...) {
    char msg[1400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_s(msg, sizeof msg, _TRUNCATE, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_log, "%02u:%02u:%02u.%03u %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
        fflush(g_log);
    }
}
void host_log(const char* msg) {
    log("%s", msg);
#ifdef WB_MOD_PLATFORM
    if (g_mod) g_platform->log(g_mod, msg);
#else
    if (g_context && g_context->log_utf8) g_context->log_utf8(msg);
#endif
}
bool readable(const void* ptr, size_t n) {
    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    if (p < 0x10000 || !n || p + n < p) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(ptr, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    return p+n <= reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
}
bool contains(const char* s, const char* token) {
    if (!s || !token || !*token) return false;
    const size_t n = strlen(token);
    for (; *s; ++s) if (_strnicmp(s, token, n) == 0) return true;
    return false;
}
bool any(const char* s, std::initializer_list<const char*> words) {
    for (auto w : words) if (contains(s,w)) return true;
    return false;
}
std::string stem(const char* name) {
    std::string s(name ? name : "");
    std::replace(s.begin(),s.end(),'\\','/');
    auto slash=s.find_last_of('/');
    if (slash!=std::string::npos) s.erase(0,slash+1);
    for (char& c:s) if (c>='A' && c<='Z') c+=32;
    auto dot=s.find_last_of('.'); if (dot!=std::string::npos) s.resize(dot);
    for (auto suffix : {"_template", "_lod0", "_3d", "_low"}) {
        size_t n=strlen(suffix);
        if (s.size()>n && s.compare(s.size()-n,n,suffix)==0) { s.resize(s.size()-n); break; }
    }
    return s;
}
struct MsvcString {
    union { char small[16]; const char* heap; };
    size_t length, capacity;
    const char* get() const { return capacity<16 ? small : heap; }
};
static_assert(sizeof(MsvcString)==32);
struct GameVector {
    byte *first, *last, *end;
    size_t count(size_t element) const {
        if (!first) return 0;
        return last>=first && (last-first)%element==0 && last-first<0x40000000 ? (last-first)/element : SIZE_MAX;
    }
};
struct Group { int32_t definition, model, start, count, billboard_batch, billboard_start, billboard_count; };
static_assert(sizeof(Group)==28);
const char* string_at(byte* p) {
    auto s=reinterpret_cast<MsvcString*>(p);
    if (!readable(s,sizeof *s) || s->length>1024 || s->capacity<s->length || !readable(s->get(),s->length+1)) return nullptr;
    return s->get();
}
using SerializeFn = void (*)(byte*, void*, void*, void*);
using ProceduralDestructorFn = void (*)(byte*);
using ResizeFn = void (*)(GameVector*, size_t);
using DecodeFn = float* (*)(byte*, float*, const byte*, float*);
using LookupFn = byte* (*)(byte*, uint32_t);
using ModelFn = int (*)(byte*, uint32_t);
// FH3 adds a SEVENTH argument to FH2's six-argument mesh query.
using QueryFn = int (*)(byte*, const float*, int, byte**, byte**, uint8_t, void*);
using CollisionFn = void (*)(byte*, void*);
using BindLooseModelFn = void (*)(byte*, int, byte*, int, const float*);
using BreakPiecesFn = void (*)(byte*,byte*);
using ObjectDefinitionIdFn = uint32_t* (*)(byte*,uint32_t*);
using ReleaseObjectFn = void (*)(byte*,byte*,unsigned);
using TableConstructorFn = byte* (*)(byte*);
using TableDestructorFn = byte* (*)(byte*,unsigned);
using ModelBindFn = uintptr_t (*)(byte*,uint32_t);
using AddCellFn = void (*)(byte*,byte*);
using WorldUpdateFn = void (*)(byte*,const float*,float,float);
SerializeFn o_serialize{};
ProceduralDestructorFn o_procedural_destructor{};
LookupFn o_lookup{};
ModelFn o_model{};
QueryFn o_query{};
BindLooseModelFn o_bind_loose_model{};
BreakPiecesFn o_break_pieces{};
ObjectDefinitionIdFn o_object_definition_id{};
CollisionFn o_collision{};
TableConstructorFn o_table_constructor{};
TableDestructorFn o_table_destructor{};
ModelBindFn o_model_bind{};
AddCellFn o_add_cell{};
WorldUpdateFn o_world_update{};
std::atomic<byte*> g_candidate_table{};
std::atomic<uint64_t> g_table_epoch{1};

struct NativeDefinition { uint16_t id; byte* value; std::string name; };
enum class LooseVisual { Native, OriginalProp, ConcreteBench, Fragments };
constexpr int BENCH_MODELS[]{45347,45348,45349}; // Bench_02, LOD0 through LOD2.
struct Binding { byte* definition; uint16_t native; LooseVisual loose_visual{}; bool large_palm{}; };
struct Definitions {
    byte* table;
    byte* list;
    size_t count;
    byte* first;
    byte* end_token;
    uint64_t header_token;
    std::atomic<bool> alive{true};
    std::vector<NativeDefinition> items;
    std::bitset<4096> utility_poles;
    bool australia{};
    std::array<std::atomic<Binding*>,CLUTTER_END-TREE_FIRST> bindings{};
    std::unordered_set<uint32_t> missing;
};
std::mutex g_def_mutex;
std::vector<Definitions*> g_definitions;
std::atomic<Definitions*> g_active_definitions{};
std::atomic<uint64_t> g_definition_scans{},g_definition_cache_hits{};
std::mutex g_deferred_mutex;
std::unordered_set<byte*> g_deferred_data;
std::atomic<uint64_t> g_late_passes{};
std::mutex g_alias_mutex;
struct AliasRecipe {
    std::string donor;
    wb::Kind kind{};
    uint32_t fallback{};
    int model=-1;
    uint8_t tier=4;
    bool fracture{};
    bool large_palm{};
};
std::array<AliasRecipe,CLUTTER_END-CLUTTER_FIRST> g_alias_recipes;
uint32_t g_clutter_count{};

uint32_t scenery_alias(const std::string& donor, wb::Kind kind, uint32_t fallback, int model, uint8_t tier=4, bool fracture=false, bool large_palm=false) {
    std::lock_guard<std::mutex> lock(g_alias_mutex);
    for (uint32_t i=0;i<g_clutter_count;++i) {
        const auto& recipe=g_alias_recipes[i];
        if (recipe.model==model && recipe.kind==kind && recipe.tier==tier &&
            recipe.fallback==fallback && recipe.donor==donor && recipe.fracture==fracture &&
            recipe.large_palm==large_palm) return CLUTTER_FIRST+i;
    }
    if (g_clutter_count==g_alias_recipes.size()) return 0;
    g_alias_recipes[g_clutter_count]={donor,kind,fallback,model,tier,fracture,large_palm};
    return CLUTTER_FIRST+g_clutter_count++;
}
Definitions* definitions(byte* table) {
    if (!readable(table,24)) return nullptr;
    byte* list=*reinterpret_cast<byte**>(table+8);
    byte* end=*reinterpret_cast<byte**>(table+0x10);
    if (!readable(list,8)) return nullptr;
    byte* first=*reinterpret_cast<byte**>(list);
    for (auto d:g_definitions) if (d->alive.load(std::memory_order_acquire) && d->table==table && d->list==list && d->end_token==end && d->first==first) return d;
    // Only +8 is guaranteed by the native FH3 lookup. +0x10 is NOT always
    // a vector end. FH2's original mod also validates it before using it.
    uintptr_t a=reinterpret_cast<uintptr_t>(list),b=reinterpret_cast<uintptr_t>(end);
    bool native_count=*reinterpret_cast<uintptr_t*>(table)==reinterpret_cast<uintptr_t>(g_base)+TABLE_VTABLE;
    if (native_count && (!b || b>4096)) return nullptr;
    bool has_end=!native_count && b>a && (b-a)%8==0 && (b-a)/8<=4096;
    // FH3's table destructor (1D2FCD0) explicitly uses +0x10 as a COUNT.
    size_t count=native_count?b:has_end?(b-a)/8:4096;
    auto d=new Definitions{};
    d->table=table;d->list=list;d->count=count;d->first=first;d->end_token=end;
    d->header_token=*reinterpret_cast<uint64_t*>(table);
    g_definition_scans.fetch_add(1,std::memory_order_relaxed);
    unsigned misses=0,typed=0;
    for (size_t i=0;i<count && misses<64;++i) {
        byte** slot=reinterpret_cast<byte**>(list)+i;
        if (!readable(slot,8)) break;
        byte* p=*slot;
        if (!readable(p,DEFINITION_SIZE) || *reinterpret_cast<uintptr_t*>(p)!=reinterpret_cast<uintptr_t>(g_base)+DEF_VTABLE) { ++misses;continue; }
        misses=0;
        ++typed;
        const char* path=reinterpret_cast<const char*>(p+0x11C);
        if (!memchr(path,0,256)) continue;
        d->items.push_back({static_cast<uint16_t>(i),p,stem(path)});
        if (wb::is_utility_pole(d->items.back().name)) d->utility_poles.set(i);
    }
    if (native_count && typed+1<count) { delete d;return nullptr; } // table is still being filled
    if (d->items.empty()) {
        static std::unordered_set<byte*> reported;
        if (reported.insert(table).second) {
            uintptr_t actual=readable(first,8)?*reinterpret_cast<uintptr_t*>(first):0;
            log("DEFERRED: definition table unrecognized: table %p, list %p, +10 %p, first %p, vtable %p, expected %p; injection skipped",table,list,end,first,reinterpret_cast<void*>(actual),g_base+DEF_VTABLE);
        }
        delete d; return nullptr;
    }
    d->australia=std::any_of(d->items.begin(),d->items.end(),[](const NativeDefinition& item){
        return item.id==367 && item.name=="tree_gorge_ghostgum_04_smashable" &&
               *reinterpret_cast<const int*>(item.value+0xC0)==2205;
    });
    g_definitions.push_back(d);
    log("FH3 definition table: %zu roots (%s).", d->items.size(),native_count?"native pointer array + count":has_end?"bounded vector":"bounded pointer-table scan");
    if (g_debug) for (auto& item:d->items)
        log("definition %u: %s; pieces %u, mass %.2f, type %u",item.id,item.name.c_str(),*reinterpret_cast<uint32_t*>(item.value+0x94),*reinterpret_cast<float*>(item.value+DEFINITION_MASS),item.value[0xE0]);
    return d;
}
const NativeDefinition* select_definition(Definitions* d, uint32_t id) {
    if (id>=CLUTTER_FIRST && id<CLUTTER_END) {
        AliasRecipe recipe;
        {
            std::lock_guard<std::mutex> lock(g_alias_mutex);
            if (id-CLUTTER_FIRST>=g_clutter_count) return nullptr;
            recipe=g_alias_recipes[id-CLUTTER_FIRST];
        }
        for (auto& item:d->items)
            if (item.name==recipe.donor && *reinterpret_cast<const uint32_t*>(item.value+0x74)==0) return &item;
        return recipe.fallback?select_definition(d,recipe.fallback):nullptr;
    }
    const bool tree=id<TREE_FIRST+TREE_TIERS;
    if (id>=WOOD_FENCE && id<BARRIER_END) id=WOOD_FENCE+(id-WOOD_FENCE)%3;
    const NativeDefinition* best=nullptr;
    int best_score=-1;
    for (auto& item:d->items) {
        if (*reinterpret_cast<const uint32_t*>(item.value+0x74)!=0) continue;
        const char* n=item.name.c_str();
        int score=-1;
        uint32_t pieces=*reinterpret_cast<uint32_t*>(item.value+0x94);
        if (tree) {
            if (contains(n,"tree_gorge_ghostgum_04_smashable")) score=100;
            else if (contains(n,"tree_") && contains(n,"smashable") && !contains(n,"_b") && pieces) score=80;
            else if (contains(n,"tree_") && pieces && !any(n,{"branch","stump","root"})) score=50;
        } else if (id==WOOD_FENCE) {
            if (any(n,{"barr_fence_wood","barr_fence_picket","woodfence"})) score=100;
            else if (contains(n,"fence") && !any(n,{"festival","fest_","crowd","concrete"})) score=40;
        } else if (id==WIRE_FENCE || id==METAL_FENCE) {
            if (contains(n,"fence") && !any(n,{"festival","fest_","crowd","concrete"}))
                score=any(n,{"wire","chain","metal"})?100:40;
        }
        if (score>best_score) { best=&item; best_score=score; }
    }
    return best;
}
bool same_table(Definitions* d,byte* table) {
    return d && d->alive.load(std::memory_order_acquire) && d->table==table && *reinterpret_cast<byte**>(table+8)==d->list && *reinterpret_cast<byte**>(table+0x10)==d->end_token && *reinterpret_cast<uint64_t*>(table)==d->header_token && *reinterpret_cast<byte**>(d->list)==d->first;
}
void fit_palm_trunk(byte* root,const byte* donor_shape,float scale) {
    // Native type 5 is a convex hull of spheres. The standing body uses a
    // rounded trunk of the placed size. Debris keeps the native piece hulls.
    auto list=reinterpret_cast<byte**>(root+DEFINITION_SIZE);
    auto shape=root+DEFINITION_SIZE+0x10;
    auto spheres=reinterpret_cast<float(*)[4]>(shape+0x20);
    memcpy(shape,donor_shape,0x20);
    const float radius=1.12f*scale+0.08f;
    const float lower=radius-0.18f,upper=std::max(lower,2.1f*scale);
    const float trunk[2][4]={{0,lower,0,radius},{0,upper,0,radius}};
    memcpy(spheres,trunk,sizeof trunk);
    *reinterpret_cast<float(**)[4]>(shape+8)=spheres;
    *reinterpret_cast<uint32_t*>(shape+0x10)=2;
    list[0]=shape;
    *reinterpret_cast<byte***>(root+0x80)=list;
    *reinterpret_cast<uint32_t*>(root+0x90)=1;
    const float half_height=(upper-lower)*0.5f+radius;
    const float center=(upper+lower)*0.5f;
    const float bounds[]{radius,half_height,radius,0,center,0,0,0,0};
    memcpy(root+0x50,bounds,sizeof bounds); // Half extents, bounds center, and graphics offset.
    *reinterpret_cast<float*>(root+0x98)=upper+radius;
    // FH3's contact test reads the donor's relative impact threshold at D4.
    // Allow an ordinary low-speed hit to reach the native break logic.
    float& threshold=*reinterpret_cast<float*>(root+0xD4);
    threshold=std::clamp(threshold*0.35f,0.1f,3.0f);
}
void fit_bench_hull(byte* root,const byte* donor_shape) {
    // The original bench is a low concrete box with a timber cap. Use the
    // native bench's point-cloud shape type with the eight measured corners.
    auto list=reinterpret_cast<byte**>(root+0x800);
    auto shape=root+0x810;
    auto corners=reinterpret_cast<float(*)[4]>(shape+0x30);
    memcpy(shape,donor_shape,0x30);
    const float half[]{1.93f,0.245f,0.30f};
    const float center[]{0,0.22f,-0.275f};
    for (unsigned i=0;i<8;++i) for (unsigned k=0;k<3;++k)
        corners[i][k]=center[k]+((i&(1u<<k))?half[k]:-half[k]);
    *reinterpret_cast<float(**)[4]>(shape+8)=corners;
    *reinterpret_cast<uint32_t*>(shape+0x10)=8;
    *reinterpret_cast<uint64_t*>(shape+0x18)=0xB0; // Native shape's inline byte size.
    list[0]=shape;
    *reinterpret_cast<byte***>(root+0x80)=list;
    *reinterpret_cast<uint32_t*>(root+0x90)=1;
    const float bounds[]{half[0],half[1],half[2],center[0],center[1],center[2],0,0,0};
    memcpy(root+0x50,bounds,sizeof bounds);
    *reinterpret_cast<float*>(root+0x98)=2.1f;
    *reinterpret_cast<float*>(root+0xD4)=3.0f;
    auto inertia=reinterpret_cast<float*>(root+8);
    auto inverse=reinterpret_cast<float*>(root+0x2C);
    std::fill_n(inertia,9,0.0f);
    std::fill_n(inverse,9,0.0f);
    // The native definition reader stores Mass at +9C. The bytes at
    // +B4 belong to the collision geometry vector's capacity pointer.
    const float mass=*reinterpret_cast<const float*>(root+DEFINITION_MASS);
    for (unsigned k=0;k<3;++k) {
        const unsigned a=(k+1)%3,b=(k+2)%3;
        const float moment=mass*((half[a]*half[a]+half[b]*half[b])/3+center[a]*center[a]+center[b]*center[b]);
        inertia[k*4]=moment;
        inverse[k*4]=1.0f/moment;
    }
}
byte* resolve_definition(byte* table, uint32_t id, uint16_t* native) {
    // Existing aliases need only immutable pointer reads. Do not take a mutex,
    // query virtual memory, or scan the table on every physics lookup.
    Definitions* current=g_active_definitions.load(std::memory_order_acquire);
    if (same_table(current,table)) {
        if (auto binding=current->bindings[id-TREE_FIRST].load(std::memory_order_acquire)) {
            *native=binding->native;
            g_definition_cache_hits.fetch_add(1,std::memory_order_relaxed);
            return binding->definition;
        }
    }
    // An unsupported/unready table is retried at most twice a second per
    // thread. The previous build retried on every single object lookup.
    struct Failure { byte* table{};byte* list{};byte* first{};byte* last{};uint64_t epoch{};ULONGLONG until{}; };
    static thread_local Failure failure;
    byte* list=*reinterpret_cast<byte**>(table+8);
    byte* first=list?*reinterpret_cast<byte**>(list):nullptr;
    uint64_t count=*reinterpret_cast<uint64_t*>(table+0x10);
    bool native_count=*reinterpret_cast<uintptr_t*>(table)==reinterpret_cast<uintptr_t>(g_base)+TABLE_VTABLE;
    byte* last=list && native_count && count>0 && count<=4096?reinterpret_cast<byte**>(list)[count-1]:first;
    uint64_t epoch=g_table_epoch.load(std::memory_order_acquire);
    ULONGLONG now=GetTickCount64();
    if (failure.table==table && failure.list==list && failure.first==first && failure.last==last && failure.epoch==epoch && now<failure.until) return nullptr;
    std::lock_guard<std::mutex> definitions_lock(g_def_mutex);
    auto d=definitions(table);
    if (!d) { failure={table,list,first,last,epoch,now+500};return nullptr; }
    g_active_definitions.store(d,std::memory_order_release);
    if (auto binding=d->bindings[id-TREE_FIRST].load(std::memory_order_acquire)) { *native=binding->native;return binding->definition; }
    if (d->missing.count(id)) return nullptr;
    auto item=select_definition(d,id);
    if (!item) { d->missing.insert(id);d->bindings[id-TREE_FIRST].store(new Binding{nullptr,0},std::memory_order_release);return nullptr; }
    AliasRecipe recipe;
    if (id>=CLUTTER_FIRST && id<CLUTTER_END) {
        std::lock_guard<std::mutex> alias_lock(g_alias_mutex);
        recipe=g_alias_recipes[id-CLUTTER_FIRST];
    }
    const auto piece_count=*reinterpret_cast<const unsigned*>(item->value+0x94);
    auto native_pieces=*reinterpret_cast<byte***>(item->value+0x88);
    if (recipe.fracture && (piece_count<2 || piece_count>MAX_FRAGMENTS ||
        !readable(native_pieces,piece_count*sizeof *native_pieces))) return nullptr;
    if (recipe.fracture) for (unsigned i=0;i<piece_count;++i)
        if (!readable(native_pieces[i],DEFINITION_SIZE)) return nullptr;
    const bool bench=recipe.kind==wb::Kind::Furniture && recipe.model==BENCH_MODELS[0];
    const byte* standing_shape=nullptr;
    if (recipe.large_palm || bench) {
        const auto shapes=*reinterpret_cast<byte* const* const*>(item->value+0x80);
        const auto donor=bench?"prop_surfers_bench_03":"tree_global_palm_02";
        const float mass=*reinterpret_cast<const float*>(item->value+DEFINITION_MASS);
        if (!d->australia || item->name!=donor ||
            *reinterpret_cast<const uint32_t*>(item->value+0x90)!=1 || !readable(shapes,sizeof *shapes) ||
            !readable(shapes[0],bench?0x30:0x20) || shapes[0][0]!=5 ||
            (bench && (!std::isfinite(mass) || mass<=0))) {
            d->bindings[id-TREE_FIRST].store(new Binding{nullptr,0},std::memory_order_release);
            log("scenery alias %04X skipped: no matching %s standing hull",id,donor);
            return nullptr;
        }
        standing_shape=shapes[0];
    }
    const size_t bytes=recipe.fracture?DEFINITION_SIZE*(1+piece_count)+MAX_FRAGMENTS*sizeof(byte*):0x1000;
    byte* clone=static_cast<byte*>(VirtualAlloc(nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!clone) return nullptr;
    memcpy(clone,item->value,DEFINITION_SIZE);
    if (bench) fit_bench_hull(clone,standing_shape);
    auto loose_visual=LooseVisual::Native;
    int tree_tier=id>=TREE_FIRST && id<TREE_FIRST+TREE_TIERS?int(id-TREE_FIRST):-1;
    if (id>=CLUTTER_FIRST && id<CLUTTER_END) {
        if (recipe.kind==wb::Kind::Tree && !recipe.fracture) tree_tier=recipe.tier;
        if (recipe.fracture) {
            auto pieces=reinterpret_cast<byte**>(clone+DEFINITION_SIZE);
            byte* storage=clone+DEFINITION_SIZE+MAX_FRAGMENTS*sizeof(byte*);
            for (unsigned i=0;i<piece_count;++i) {
                auto piece=storage+i*DEFINITION_SIZE;
                memcpy(piece,native_pieces[i],DEFINITION_SIZE);
                // Each fragment gets its own native model and hull. The
                // standalone renderer doesn't need the donor's model links.
                *reinterpret_cast<GameVector*>(piece+0xE8)={};
                pieces[i]=piece;
            }
            *reinterpret_cast<byte***>(clone+0x88)=pieces;
            loose_visual=LooseVisual::Fragments;
        } else if (recipe.model>=0 && d->australia) {
            *reinterpret_cast<int*>(clone+0xC0)=recipe.model;
            if (recipe.kind==wb::Kind::Tree) {
                // Breakup supplies a separate authored fracture definition.
                // A foreign fragment list would reference the wrong model IDs.
                *reinterpret_cast<byte***>(clone+0x88)=nullptr;
                *reinterpret_cast<uint32_t*>(clone+0x94)=0;
            } else {
                auto children=reinterpret_cast<byte**>(clone+DEFINITION_SIZE);
                auto child=clone+DEFINITION_SIZE+16;
                memcpy(child,clone,DEFINITION_SIZE);
                *reinterpret_cast<uint32_t*>(child+0x74)=1;
                *reinterpret_cast<byte***>(child+0x88)=nullptr;
                *reinterpret_cast<uint32_t*>(child+0x94)=0;
                *reinterpret_cast<GameVector*>(child+0xE8)={};
                children[0]=child;
                *reinterpret_cast<byte***>(clone+0x88)=children;
                *reinterpret_cast<uint32_t*>(clone+0x94)=1;
                loose_visual=bench?LooseVisual::ConcreteBench:LooseVisual::OriginalProp;
            }
        }
    }
    if (tree_tier>=0) {
        float scale=std::pow(2.0f,(tree_tier-4)/4.0f);
        *reinterpret_cast<float*>(clone+0xB4)*=g_tree_mass*std::min(8.0f,scale*scale);
        *reinterpret_cast<float*>(clone+0xC8)*=g_break_speed;
        if (scale>=g_big_tree_size) clone[0xE0]=5; // LogPiles in FH3's SmashableObjectTypes.xml.
    }
    if (recipe.large_palm) fit_palm_trunk(clone,standing_shape,std::pow(2.0f,(int(recipe.tier)-4)/4.0f));
    d->bindings[id-TREE_FIRST].store(new Binding{clone,item->id,loose_visual,recipe.large_palm},std::memory_order_release);
    *native=item->id;
    log("alias %04X -> FH3 definition %u (%s), visual model %d, %u pieces",
        id,item->id,item->name.c_str(),*reinterpret_cast<const int*>(clone+0xC0),
        *reinterpret_cast<const uint32_t*>(clone+0x94));
    return clone;
}
bool virtual_id(uint32_t id) {
    return (id>=TREE_FIRST && id<TREE_FIRST+TREE_TIERS) || (id>=WOOD_FENCE && id<BARRIER_END) ||
           (id>=CLUTTER_FIRST && id<CLUTTER_END);
}
std::atomic<uint64_t> g_alias_misses{};
std::atomic<uint64_t> g_unready_skips{};
void observe_table(byte* table) {
    auto current=g_active_definitions.load(std::memory_order_acquire);
    if (same_table(current,table)) return;
    uint16_t native{};
    // Bind trees on a normal native lookup, before injecting reserved IDs.
    resolve_definition(table,TREE_FIRST+4,&native);
}
void retire_table(byte* table) {
    std::lock_guard<std::mutex> lock(g_def_mutex);
    for (auto d:g_definitions) if (d->table==table) d->alive.store(false,std::memory_order_release);
    auto active=g_active_definitions.load(std::memory_order_acquire);
    if (active && active->table==table) g_active_definitions.compare_exchange_strong(active,nullptr,std::memory_order_acq_rel);
    g_table_epoch.fetch_add(1,std::memory_order_acq_rel);
}
byte* table_constructor(byte* table) {
    retire_table(table);
    byte* result=o_table_constructor(table);
    g_candidate_table.store(result,std::memory_order_release);
    return result;
}
byte* table_destructor(byte* table,unsigned flags) {
    byte* expected=table;
    g_candidate_table.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);
    retire_table(table);
    return o_table_destructor(table,flags);
}
void bootstrap_definitions() {
    if (g_active_definitions.load(std::memory_order_acquire)) return;
    // The table exists before the first native smashable lookup. Waiting for
    // that lookup lost scenery that was serialized earlier during world load.
    if (auto table=g_candidate_table.load(std::memory_order_acquire)) observe_table(table);
}
struct LooseVisualLookup { byte* table{}; uint32_t id{}; byte* definition{}; };
thread_local LooseVisualLookup g_loose_visual;
struct FractureSource { byte* object{}; uint32_t alias{}; };
thread_local FractureSource g_fracture_source;
uint32_t* object_definition_id(byte* object,uint32_t* out) {
    if (object==g_fracture_source.object && g_fracture_source.alias) {
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-reinterpret_cast<uintptr_t>(g_base);
        if (caller==FRACTURE_ROOT_RETURN || caller==FRACTURE_CHILD_RETURN) {
            *out=0xFFFF0000 | g_fracture_source.alias;
            return out;
        }
    }
    return o_object_definition_id(object,out);
}
byte* lookup(byte* table,uint32_t id) {
    if (g_loose_visual.definition && g_loose_visual.table==table && g_loose_visual.id==id)
        return g_loose_visual.definition;
    uint32_t low=id&0xFFFF;
    if (!virtual_id(low)) { observe_table(table);return o_lookup(table,id); }
    uint16_t native{};
    byte* d=resolve_definition(table,low,&native);
    if (!d) {
        // Do not index the native table with a reserved ID on an unexpected map.
        if (g_alias_misses.fetch_add(1)<10) log("ERROR: unresolved virtual definition %08X",id);
        return o_lookup(table,0xFFFF0000);
    }
    const uint32_t piece=id>>16;
    if (piece==0xFFFF) return d;
    if (piece>=*reinterpret_cast<const uint32_t*>(d+0x94)) return o_lookup(table,0xFFFF0000);
    return (*reinterpret_cast<byte***>(d+0x88))[piece];
}
int model(byte* table,uint32_t id) {
    uint32_t low=id&0xFFFF;
    if (!virtual_id(low)) return o_model(table,id);
    auto definition=lookup(table,id);
    return definition?*reinterpret_cast<const int*>(definition+0xC0):o_model(table,0xFFFF0000);
}
int resident_bench_model(byte* manager);
void bind_loose_model(byte* manager,int slot,byte* object,int model_index,const float* transform) {
    const auto id=*reinterpret_cast<const uint32_t*>(object+0x128);
    const auto alias=id&0xFFFF, piece=id>>16;
    if (alias<CLUTTER_FIRST || alias>=CLUTTER_END || piece==0xFFFF ||
        *reinterpret_cast<const int32_t*>(object+0x148)!=5) {
        o_bind_loose_model(manager,slot,object,model_index,transform);
        return;
    }
    auto table=*reinterpret_cast<byte**>(manager+0x4D8);
    auto defs=g_active_definitions.load(std::memory_order_acquire);
    const auto binding=same_table(defs,table)?defs->bindings[alias-TREE_FIRST].load(std::memory_order_acquire):nullptr;
    if (!binding || binding->loose_visual==LooseVisual::Native ||
        piece>=*reinterpret_cast<const unsigned*>(binding->definition+0x94)) {
        o_bind_loose_model(manager,slot,object,model_index,transform);
        return;
    }
    // The native standalone renderer skips piece definitions (+74 != 0).
    // Give just this bind the matching graphics fields. For tree fragments
    // the model offset and bounds are the piece's own, like its physical hull.
    alignas(16) byte render[DEFINITION_SIZE];
    auto graphics=binding->definition;
    if (binding->loose_visual==LooseVisual::ConcreteBench) {
        model_index=resident_bench_model(manager);
        if (model_index<0) return;
        memcpy(render,graphics,DEFINITION_SIZE);
        *reinterpret_cast<int*>(render+0xC0)=model_index;
        graphics=render;
        static std::atomic<unsigned> bench_binds{};
        if (bench_binds.fetch_add(1,std::memory_order_relaxed)<12 || g_debug)
            log("bench debris %04X: binding original model %d to slot %d",alias,model_index,slot);
    } else if (binding->loose_visual==LooseVisual::Fragments) {
        const auto body=(*reinterpret_cast<byte***>(graphics+0x88))[piece];
        memcpy(render,body,DEFINITION_SIZE);
        *reinterpret_cast<uint32_t*>(render+0x74)=0;
        graphics=render;
    }
    const auto previous=g_loose_visual;
    g_loose_visual={table,id,graphics};
    __try { o_bind_loose_model(manager,slot,object,model_index,transform); }
    __finally { g_loose_visual=previous; }
}

// Bucket links and positions stay immutable for physics readers. A provider
// holds a reference only while its injected groups exist.
constexpr int POINT_CAPACITY=1<<19, BUCKETS=1<<16;
struct Point {
    float x,y,z,radius,dx,dz;
    int32_t next;
    wb::Kind kind;
    std::atomic<uint8_t> ground;
    std::atomic<uint32_t> providers;
    float tree_scale;
    bool palm;
    uint32_t break_definition;
    std::atomic<bool> broken;
    std::array<float,9> local_axes;
};
struct PendingPoint {
    float x,y,z,radius,dx,dz; bool ground; wb::Kind kind; float tree_scale{}; bool palm{};
    uint32_t break_definition{};
    std::array<float,9> local_axes{};
};
Point* g_points{};
std::array<std::atomic<int32_t>,BUCKETS> g_heads;
std::atomic<int32_t> g_point_count{};
std::atomic<int32_t> g_active_points{};
std::mutex g_point_mutex;
std::unordered_map<byte*,std::vector<int>> g_provider_points;
uint32_t bucket(int x,int z) { return (uint32_t(x)*73856093u^uint32_t(z)*19349663u)&(BUCKETS-1); }
int cell(float x) { return int(std::floor(x/4.0f)); }
void add_points(byte* data,const std::vector<PendingPoint>& points) {
    std::lock_guard<std::mutex> lock(g_point_mutex);
    auto& references=g_provider_points[data];
    references.reserve(references.size()+points.size());
    for (const auto& point:points) {
        const auto [x,y,z,radius,dx,dz,ground,kind,tree_scale,palm,break_definition,local_axes]=point;
        if (!std::isfinite(x+y+z+radius) || std::fabs(x)>100000 || std::fabs(z)>100000) continue;
        const auto h=bucket(cell(x),cell(z));
        int index=g_heads[h].load(std::memory_order_relaxed);
        for (;index>=0;index=g_points[index].next) {
            const auto& p=g_points[index];
            if (p.kind==kind && p.palm==palm && p.break_definition==break_definition &&
                std::fabs(p.x-x)<0.08f && std::fabs(p.z-z)<0.08f && std::fabs(p.y-y)<1) break;
        }
        if (index<0) {
            index=g_point_count.load(std::memory_order_relaxed);
            if (index>=POINT_CAPACITY) continue;
            auto& p=*new (g_points+index) Point{};
            p.x=x;p.y=y;p.z=z;p.radius=radius;p.dx=dx;p.dz=dz;p.kind=kind;p.tree_scale=tree_scale;
            p.palm=palm;
            p.break_definition=break_definition;
            p.local_axes=local_axes;
            p.next=g_heads[h].load(std::memory_order_relaxed);
            g_point_count.store(index+1,std::memory_order_relaxed);
            g_heads[h].store(index,std::memory_order_release);
        }
        auto& p=g_points[index];
        if (!p.providers.load(std::memory_order_relaxed)) {
            p.ground.store(ground?3:0,std::memory_order_relaxed);
            p.broken.store(false,std::memory_order_relaxed);
            g_active_points.fetch_add(1,std::memory_order_relaxed);
        }
        p.providers.fetch_add(1,std::memory_order_release);
        references.push_back(index);
    }
}
void forget_points(byte* data) {
    std::lock_guard<std::mutex> lock(g_point_mutex);
    auto found=g_provider_points.find(data);
    if (found==g_provider_points.end()) return;
    for (int index:found->second) {
        if (g_points[index].providers.fetch_sub(1,std::memory_order_release)==1)
            g_active_points.fetch_sub(1,std::memory_order_relaxed);
    }
    g_provider_points.erase(found);
}
int nearby(float x0,float z0,float x1,float z1,Point** out,int maximum,wb::SceneryMask mask) {
    int a=cell(x0),b=cell(z0),c=cell(x1),d=cell(z1),count=0;
    if (c-a>24 || d-b>24) return 0;
    for (int x=a;x<=c;++x) for (int z=b;z<=d;++z)
        for (int k=g_heads[bucket(x,z)].load(std::memory_order_acquire);k>=0;k=g_points[k].next) {
            auto& p=g_points[k];
            if ((mask&wb::kind_bit(p.kind)) && p.x>=x0 && p.x<=x1 && p.z>=z0 && p.z<=z1 &&
                p.providers.load(std::memory_order_acquire) && count<maximum) out[count++]=&p;
        }
    return count;
}
std::atomic<uint64_t> g_procedurals{},g_instances{},g_removed{},g_queries{},g_hits{};
std::atomic<uint64_t> g_cell_retries{},g_clutter_instances{},g_debris_cleared{};
std::atomic<bool> g_probe_requested{};
std::mutex g_seen_mutex;
std::unordered_set<std::string> g_seen;

bool event_only(const char* s) {
    if (!s) return true;
    for (;*s;) {
        while (*s==' ' || *s==';') ++s;
        if (*s && *s!='~') return true;
        while (*s && *s!=';') ++s;
    }
    return false;
}
const NativeDefinition* exact_definition(const Definitions* d,std::string_view name) {
    for (auto& item:d->items) {
        if (item.name==name && *reinterpret_cast<const uint32_t*>(item.value+0x74)==0) return &item;
    }
    return nullptr;
}
bool named_template(const std::string& name) {
    auto first=std::begin(native_clutter_names),last=std::end(native_clutter_names);
    auto found=std::lower_bound(first,last,name,[](const char* a,const std::string& b){return a<b;});
    return found!=last && name==*found;
}
int original_model(const std::string& name) {
    auto first=std::begin(australia_scenery_models),last=std::end(australia_scenery_models);
    auto found=std::lower_bound(first,last,name,[](const SceneryModel& a,const std::string& b){return a.name<b;});
    return found!=last && name==found->name?found->index:-1;
}
const char* stock_barrier_donor(const std::string& name) {
    // Australia PGEO records show these families without a native group.
    // Use four-metre donors for Armco spans and single posts for the uprights.
    if (name=="barr_armco_wooden_fence_4m") return "barr_fence_wooden_03_4m";
    if (name=="barr_armco_wooden_post") return "barr_fence_pickett_01_post";
    if (name=="barr_armco_01_4m" || name=="barr_plains_armco_straight_01" ||
        name=="barr_plains_armco_left_01" || name=="barr_plains_armco_right_01") return "barr_fence_metalmesh_02_4m";
    if (name=="barr_armco_01_post_left" || name=="barr_armco_01_post_right" ||
        name=="barr_metal_railing_01_post" || name=="barr_metal_railing_01_endpost" ||
        name=="barr_metal_railing_02_post") return "barr_fence_metalmesh_01_post";
    if (name=="barr_plains_bollard_01_var_a") return "barr_plains_bollard_01";
    return nullptr;
}
void inject(byte* data) {
    if (!g_scenery_mask.load(std::memory_order_acquire)) return;
    const auto rules=current_rules();
    bootstrap_definitions();
    auto definition_state=g_active_definitions.load(std::memory_order_acquire);
    if (!definition_state) {
        std::lock_guard<std::mutex> lock(g_deferred_mutex);
        g_deferred_data.insert(data);
        g_unready_skips.fetch_add(1,std::memory_order_relaxed);
        // Tree/fence aliases belong to this verified executable and are
        // resolved against the world's table when native collision objects
        // are created. Install their groups during serialization even if the
        // table is not populated yet: the provider decides whether to register
        // for collision immediately after serialization, before model binding.
    }
    const char* lib=string_at(data);
    if (!lib) return;
    auto groups=reinterpret_cast<GameVector*>(data+0x60);
    auto models=reinterpret_cast<GameVector*>(data+0xA0);
    size_t old=groups->count(sizeof(Group)),count=models->count(MODEL_BATCH_SIZE);
    if (old>8192 || count>4096 || !count || !readable(models->first,count*MODEL_BATCH_SIZE)) return;
    {
        std::lock_guard<std::mutex> lock(g_seen_mutex);
        if (g_debug && g_seen.insert(lib).second) log("procedural %s: %zu groups, %zu model batches; events '%s'",lib,old,count,string_at(data+0x20)?string_at(data+0x20):"<invalid>");
    }
    const bool for_event=event_only(string_at(data+0x20));
    if ((for_event && rules.skip_events) || wb::list_match(rules.exclude_libraries,lib)) return;
    std::vector<std::vector<wb::Range>> covered(count);
    if (old && !readable(groups->first,old*sizeof(Group))) return;
    for (size_t i=0;i<old;++i) {
        auto& g=reinterpret_cast<Group*>(groups->first)[i];
        if (!g.count) continue;
        if (g.model<0 || size_t(g.model)>=count || g.count<0 || g.count>=65536 || g.start<0 || g.start>=65536) return;
        auto batch=reinterpret_cast<const GameVector*>(models->first+g.model*MODEL_BATCH_SIZE);
        if (static_cast<size_t>(g.start+g.count)>batch->count(0x20)) return;
        covered[g.model].push_back({g.start,g.start+g.count});
    }
    std::vector<Group> additions;
    std::vector<PendingPoint> points;
    auto decode=reinterpret_cast<DecodeFn>(g_base+DECODE_INSTANCE);
    for (size_t i=0;i<count;++i) {
        byte* batch=models->first+i*MODEL_BATCH_SIZE;
        const char* name=string_at(batch+MODEL_BATCH_NAME);
        auto instances=reinterpret_cast<GameVector*>(batch);
        size_t n=instances->count(0x20);
        if (!name || !n || n>65535 || !readable(instances->first,n*0x20)) continue;
        auto holes=wb::uncovered_ranges(static_cast<int>(n),covered[i]);
        if (holes.empty()) continue;
        const auto kind=wb::classify(name,lib);
        if (!wb::selected(kind,name,lib,for_event,rules)) continue;
        const bool tree=kind==wb::Kind::Tree;
        const bool bench=kind==wb::Kind::Furniture;
        const bool chain=wb::is_barrier(kind) && kind!=wb::Kind::Bollard && !contains(name,"post");
        const auto source=stem(name);
        const bool palm=tree && contains(name,"palm");
        const bool large_palm=tree && source=="tree_global_palm_01";
        auto donor=source;
        const auto* exact=definition_state?exact_definition(definition_state,source):nullptr;
        const bool own_physics=exact || (!definition_state && named_template(source));
        const int model_index=own_physics?-1:original_model(source);
        if (!own_physics && (model_index<0 || (definition_state && !definition_state->australia) ||
                             (!definition_state && !bench && !contains(lib,"australia_")))) continue;

        uint32_t fallback=0;
        if (!own_physics) {
            donor.clear();
            if (tree) {
                fallback=TREE_FIRST+4;
                if (palm) donor="tree_global_palm_02";
                else if (contains(name,"pine")) donor="tree_global_pine_01";
            } else if (bench) {
                donor="prop_surfers_bench_03";
            } else if (const char* mapped=stock_barrier_donor(source)) {
                donor=mapped;
            } else if (chain && kind!=wb::Kind::Guardrail) {
                const auto base=any(name,{"chain","wire"})?WIRE_FENCE:any(name,{"metal","rail"})?METAL_FENCE:WOOD_FENCE;
                fallback=base+(kind==wb::Kind::Railing?3:0);
            }
            if (!fallback && donor.empty()) continue;
        }
        const auto id=tree?0:scenery_alias(donor,kind,fallback,model_index);
        if (!tree && !id) continue;
        std::vector<bool> add_instance(n,false);
        for (auto range:holes) for (int k=range.begin;k<range.end;++k) add_instance[k]=true;
        uint16_t native{};
        if (!tree && definition_state && !resolve_definition(definition_state->table,id,&native)) continue;
        struct Transform { float x,y,z,size,yaw,scale; };
        std::vector<Transform> transforms;
        transforms.reserve(n);
        std::vector<std::array<float,9>> bench_axes;
        if (bench) bench_axes.reserve(n);
        for (size_t k=0;k<n;++k) {
            alignas(16) float m[16]{}; float scale=1;
            decode(data,m,instances->first+k*0x20,&scale);
            transforms.push_back({m[12],m[13],m[14],scale*(tree?wb::size_factor(rules.size_rules,name):1.0f),std::atan2(m[2],m[0]),scale});
            if (bench) {
                auto& axes=bench_axes.emplace_back();
                for (unsigned row=0;row<3;++row) for (unsigned col=0;col<3;++col)
                    axes[row*3+col]=m[row*4+col]/(scale*scale);
            }
        }
        size_t point_start=points.size();
        std::array<uint32_t,TREE_TIERS> tiers{};
        for (size_t k=0;k<n;++k) {
            if (!add_instance[k]) continue;
            auto& t=transforms[k];
            // All audited Bench 02 placements use unit scale. The loose
            // body is sized for that mesh, so skip non-stock scaled copies.
            if (bench && !(std::fabs(t.scale-1)<0.025f)) continue;
            uint32_t def=id;
            if (tree) {
                if (!std::isfinite(t.size)) continue;
                const float size=std::max(large_palm?t.scale:t.size,0.25f);
                const float level=std::log2(size)*4;
                int tier=std::clamp(int(large_palm?std::ceil(level):std::round(level))+4,0,int(TREE_TIERS)-1);
                auto& variant=tiers[tier];
                if (!variant) variant=scenery_alias(donor,kind,fallback,model_index,static_cast<uint8_t>(tier),false,large_palm);
                if (!variant) continue;
                def=variant;
            }
            if (definition_state && !resolve_definition(definition_state->table,def,&native)) continue;
            if (!additions.empty() && additions.back().definition==int(def) && additions.back().model==int(i) && additions.back().start+additions.back().count==int(k)) ++additions.back().count;
            else additions.push_back({int(def),int(i),int(k),1,-1,0,0});
            if (!chain) {
                if (bench) {
                    points.push_back({t.x,t.y,t.z,2.0f,1,0,true,kind,0,false,def,bench_axes[k]});
                    continue;
                }
                float r=tree?std::max(0.5f,large_palm?t.scale:t.size):-std::clamp(exact?*reinterpret_cast<const float*>(exact->value+0xB0)*t.size:1.0f,0.5f,2.5f);
                points.push_back({t.x,t.y,t.z,r,1,0,true,kind,tree?t.size:0,palm,large_palm?def:0});
                continue;
            }
            float length=3,dx=std::cos(t.yaw),dz=std::sin(t.yaw),nearest=1e9f;
            for (size_t q=0;q<n && n<=4096;++q) if (q!=k) {
                float x=transforms[q].x-t.x,y=transforms[q].y-t.y,z=transforms[q].z-t.z,d=x*x+y*y+z*z;
                if (d>0.01f && d<nearest) { nearest=d;float l=std::sqrt(x*x+z*z);if(l>0.001f){dx=x/l;dz=z/l;} }
            }
            if (nearest<1e8f) length=std::clamp(std::sqrt(nearest),0.5f,8.0f);
            for (float s=-length*0.5f;s<=length;s+=0.8f) points.push_back({t.x+dx*s,t.y,t.z+dz*s,-g_barrier_radius,dx,dz,!g_ground_check,kind});
        }
        if (g_debug) log("inject %s: %zu instances, virtual %04X, %zu collision points",name,n,id,points.size()-point_start);
        if (exact || id>=CLUTTER_FIRST) g_clutter_instances.fetch_add(points.size()-point_start,std::memory_order_relaxed);
    }
    if (additions.empty() || old+additions.size()>65535) return;
    reinterpret_cast<ResizeFn>(g_base+RESIZE_GROUPS)(groups,old+additions.size());
    if (groups->count(sizeof(Group))!=old+additions.size()) { log("ERROR: native group resize failed");return; }
    memcpy(groups->first+old*sizeof(Group),additions.data(),additions.size()*sizeof(Group));
    add_points(data,points);
    g_procedurals.fetch_add(1);g_instances.fetch_add(points.size());
}
bool take_deferred(byte* data);
bool has_injected_groups(byte* data) {
    auto groups=reinterpret_cast<const GameVector*>(data+0x60);
    const size_t count=groups->count(sizeof(Group));
    if (!count || count>65535 || !readable(groups->first,count*sizeof(Group))) return false;
    auto first=reinterpret_cast<const Group*>(groups->first);
    return std::any_of(first,first+count,[](const Group& group){return virtual_id(group.definition);});
}
void serialize(byte* data,void* format,void* archive,void* name) {
    take_deferred(data);
    o_serialize(data,format,archive,name);
    __try {
        if (!has_injected_groups(data)) forget_points(data);
        inject(data);
    } __except(EXCEPTION_EXECUTE_HANDLER) { log("ERROR: procedural injection rejected an unexpected data layout"); }
}
bool take_deferred(byte* data) {
    std::lock_guard<std::mutex> lock(g_deferred_mutex);
    return g_deferred_data.erase(data)!=0;
}
void procedural_destructor(byte* data) {
    forget_points(data);
    take_deferred(data);
    o_procedural_destructor(data);
}
uintptr_t model_bind(byte* data,uint32_t kind) {
    auto result=o_model_bind(data,kind);
    // Retry on the game's owning load path, before the provider builds its
    // render/collision children. Never keep and mutate pending game pointers
    // later from a timer or worker thread.
    if (take_deferred(data)) {
        __try { inject(data);g_late_passes.fetch_add(1,std::memory_order_relaxed); }
        __except(EXCEPTION_EXECUTE_HANDLER) { log("ERROR: deferred procedural injection rejected an unexpected data layout"); }
    }
    return result;
}
void prepare_cell(byte* manager,byte* message) {
    // CWorldCollObjMan::AddCell supplies the actual world table. This is the
    // last owning load path before the world enumerates the provider's groups.
    // A transient empty table must not strand data that model binding deferred.
    observe_table(*reinterpret_cast<byte**>(manager+0x4D8));
    auto provider=*reinterpret_cast<byte**>(message+0x60);
    if (!readable(provider,0x1D0) || *reinterpret_cast<uintptr_t*>(provider)!=reinterpret_cast<uintptr_t>(g_base)+TREE_PROVIDER_VTABLE) return;
    auto data=*reinterpret_cast<byte**>(provider+0x1C8);
    if (data && take_deferred(data)) {
        inject(data);
        auto groups=reinterpret_cast<GameVector*>(data+0x60);
        auto count=groups->count(sizeof(Group));
        if (count>65535 || (count && !readable(groups->first,count*sizeof(Group)))) return;
        uint64_t children=0;
        for (size_t i=0;i<count;++i) {
            const auto& group=reinterpret_cast<const Group*>(groups->first)[i];
            children+=std::max(group.count,group.billboard_count);
        }
        // +68 was prepared by the sender before this retry. Keep the native
        // cell's child count in sync without mutating the sender's message.
        if (children<=65535) *reinterpret_cast<uint16_t*>(message+0x68)=static_cast<uint16_t>(children);
        g_cell_retries.fetch_add(1,std::memory_order_relaxed);
    }
}
void add_cell(byte* manager,byte* message) {
    alignas(16) byte local[0x70];
    memcpy(local,message,sizeof local);
    __try { prepare_cell(manager,local); }
    __except(EXCEPTION_EXECUTE_HANDLER) { log("ERROR: cell retry rejected an unexpected provider layout"); }
    o_add_cell(manager,local);
}

void triangle(const byte* tri,const byte* vertices,const float*& a,const float*& b,const float*& c) {
    auto v=reinterpret_cast<const float*>(vertices);
    a=v+*reinterpret_cast<const uint32_t*>(tri+4)*4;
    b=v+*reinterpret_cast<const uint32_t*>(tri+8)*4;
    c=v+*reinterpret_cast<const uint32_t*>(tri+12)*4;
}
void ground_probe(Point* p,byte** triangles,byte** vertices,int n) {
    uint8_t found=p->ground.load(std::memory_order_relaxed);
    if (found==3) return;
    float dx=-p->dz,dz=p->dx;
    for (int side=0;side<2;++side) {
        if (found&(1<<side)) continue;
        float x=p->x+dx*3*(side?-1:1),z=p->z+dz*3*(side?-1:1);
        for (int k=0;k<n;++k) {
            const float *a,*b,*c; triangle(triangles[k],vertices[k],a,b,c);
            float ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
            float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            float area=ux*vz-uz*vx;
            if (std::fabs(area)<1e-9f || ny*ny<0.5f*(nx*nx+ny*ny+nz*nz)) continue;
            float t=((x-a[0])*vz-(z-a[2])*vx)/area,u=(ux*(z-a[2])-uz*(x-a[0]))/area;
            float y=a[1]+t*uy+u*vy;
            if (t>=0 && u>=0 && t+u<=1 && std::fabs(y-p->y)<=1.5f) { p->ground.fetch_or(uint8_t(1<<side)); break; }
        }
    }
}
enum class CollisionMesh { Protected, Terrain, Procedural, ProceduralProps };

CollisionMesh collision_mesh(const byte* mesh) {
    // The scratch-free query callers pass the mesh at +D8 of the template.
    // Both archive readers deserialize its resource name into the string at +0.
    const auto& name=*reinterpret_cast<const MsvcString*>(mesh-0xD8);
    if (name.length<8 || name.length>128 || name.capacity<name.length) return CollisionMesh::Protected;
    const std::string_view path(name.get(),name.length);
    const auto slash=path.find_last_of("/\\");
    const auto file=path.substr(slash==std::string_view::npos?0:slash+1);
    auto prefix=[&](std::string_view value) {
        return file.size()>=value.size() && _strnicmp(file.data(),value.data(),value.size())==0;
    };
    auto suffix=[&](std::string_view value) {
        return file.size()>=value.size() && _strnicmp(file.data()+file.size()-value.size(),value.data(),value.size())==0;
    };
    if (prefix("autoterrain_") && suffix(".phys")) return CollisionMesh::Terrain;
    // Bench 02 appears in the mainland props layer and in these three
    // route-conditioned variants. The same footprint and materials apply.
    if (suffix(".procphys") && (prefix("Regrouped_11_square") ||
        prefix("Australia_procedural_routeset_69_square") || prefix("Australia_procedural_routeset_70_square") ||
        prefix("Australia_procedural_routeset_136_square"))) return CollisionMesh::ProceduralProps;
    if (suffix(".procphys") && (prefix("Regrouped_") || prefix("australia_procedural_routeset_")))
        return CollisionMesh::Procedural;
    return CollisionMesh::Protected;
}

bool matches_surface(wb::Kind kind,byte surface) {
    // FH3 surfaceTypes.xml indices, stored at byte 1 of each mesh triangle.
    switch (kind) {
        case wb::Kind::Tree: return surface>=85 && surface<=90; // TREE_*
        case wb::Kind::Fence: return surface==71 || surface==72 || surface==73 || surface==79 || surface==91;
        case wb::Kind::Railing: return surface==65 || surface==73 || surface==93;
        case wb::Kind::Guardrail: return surface==65 || surface==66;
        case wb::Kind::Bollard: return surface==64;
        default: return false;
    }
}

bool matches_prop(const Point& p,const float* a,const float* b,const float* c,float x,float z,bool vertical) {
    const float dx=x-p.x,dz=z-p.z;
    if (p.radius>=0) {
        const float radius=std::clamp(g_trunk_radius*std::max(p.radius,0.5f),0.35f,2.2f);
        if (dx*dx+dz*dz>radius*radius) return false;
        if (!vertical && !p.palm) return false;
        for (auto v:{a,b,c}) {
            const float h=v[1]-p.y;
            // Stock palms have closed trunks. Their nearly horizontal end
            // faces use the same TREE material as the sides of the trunk.
            const float cap=std::min(radius+0.15f,p.break_definition?1.35f*p.radius:1.25f);
            const float reach=vertical?radius+std::clamp(h*0.1f,0.15f,0.8f):cap;
            const float vx=v[0]-p.x,vz=v[2]-p.z;
            if (h<(vertical?-2.0f:-1.0f) || h>(vertical?45.0f:6.0f) || vx*vx+vz*vz>reach*reach) return false;
        }
        return true;
    }
    const float radius=std::min(-p.radius,1.15f);
    if (!vertical || dx*dx+dz*dz>radius*radius || p.ground.load(std::memory_order_relaxed)!=3) return false;
    for (auto v:{a,b,c}) {
        const float h=v[1]-p.y,vx=v[0]-p.x,vz=v[2]-p.z;
        if (h<-1.75f || h>2.75f) return false;
        if (p.kind==wb::Kind::Bollard) {
            if (vx*vx+vz*vz>radius*radius) return false;
        } else if (std::fabs(vx*p.dz-vz*p.dx)>radius || std::fabs(vx*p.dx+vz*p.dz)>4.5f) return false;
    }
    return true;
}

bool matches_bench(const Point& p,byte surface,const float* a,const float* b,const float* c) {
    if (!p.broken.load(std::memory_order_acquire)) return false;
    constexpr float minimum[]{-1.89f,0,-0.545f},maximum[]{1.89f,0.428f,-0.008f};
    constexpr float tolerance=0.055f;
    unsigned faces=0b11111; // Two ends, two long sides, and the seat. The base is open.
    float low[3]{1e9f,1e9f,1e9f},high[3]{-1e9f,-1e9f,-1e9f};
    for (const auto vertex:{a,b,c}) {
        const float delta[]{vertex[0]-p.x,vertex[1]-p.y,vertex[2]-p.z};
        float local[3]{};
        for (unsigned axis=0;axis<3;++axis) {
            const auto row=p.local_axes.data()+axis*3;
            local[axis]=row[0]*delta[0]+row[1]*delta[1]+row[2]*delta[2];
            if (!(local[axis]>=minimum[axis]-tolerance && local[axis]<=maximum[axis]+tolerance)) return false;
            low[axis]=std::min(low[axis],local[axis]);
            high[axis]=std::max(high[axis],local[axis]);
        }
        if (std::fabs(local[0]-minimum[0])>tolerance) faces&=~1u;
        if (std::fabs(local[0]-maximum[0])>tolerance) faces&=~2u;
        if (std::fabs(local[2]-minimum[2])>tolerance) faces&=~4u;
        if (std::fabs(local[2]-maximum[2])>tolerance) faces&=~8u;
        if (std::fabs(local[1]-maximum[1])>tolerance) faces&=~16u;
    }
    if (surface==91) return (faces&16) && high[0]-low[0]>0.2f && high[2]-low[2]>0.2f;
    return surface==19 && (faces&15) && high[1]-low[1]>0.28f;
}

constexpr int MAX_FILTER_TRIANGLES=1024;
bool classify_triangles(const byte* mesh,const float* box,int n,byte** triangles,byte** vertices,bool* discard) {
    const auto source=collision_mesh(mesh);
    if (source==CollisionMesh::Protected || (source==CollisionMesh::Terrain && !g_ground_check)) return false;
    const float margin=source==CollisionMesh::Terrain?3.0f:4.5f;
    Point* candidates[256];
    // Keep the collider paired with its loaded conversion after Off. Restoring
    // the static mesh here would put an invisible collider on a smashed prop.
    const int nc=nearby(box[0]-margin,box[2]-margin,box[4]+margin,box[6]+margin,candidates,256,wb::all_kinds);
    if (!nc) return false;
    if (source==CollisionMesh::Terrain) {
        for (int i=0;i<nc;++i) if (candidates[i]->radius<0) ground_probe(candidates[i],triangles,vertices,n);
        return false;
    }
    Point* standing_palms[256];
    int palm_count=0;
    for (int i=0;i<nc;++i) {
        auto p=candidates[i];
        if (p->palm && p->break_definition && !p->broken.load(std::memory_order_acquire)) standing_palms[palm_count++]=p;
        else candidates[i-palm_count]=p;
    }
    if (nc==palm_count) return false;
    bool changed=false;
    for (int k=0;k<n;++k) {
        const byte surface=triangles[k][1];
        const bool bench_surface=source==CollisionMesh::ProceduralProps && (surface==19 || surface==91);
        if (!bench_surface && (surface<64 || surface>93)) continue;
        const float *a,*b,*c; triangle(triangles[k],vertices[k],a,b,c);
        float ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        const float normal2=nx*nx+ny*ny+nz*nz;
        if (!(normal2>1e-12f)) continue;
        const bool vertical=ny*ny<0.36f*normal2;
        const float x=(a[0]+b[0]+c[0])/3,z=(a[2]+b[2]+c[2])/3;
        bool standing_trunk=false;
        if (matches_surface(wb::Kind::Tree,surface)) for (int i=0;i<palm_count;++i) {
            if (matches_prop(*standing_palms[i],a,b,c,x,z,vertical)) { standing_trunk=true;break; }
        }
        if (standing_trunk) continue;
        for (int i=0;i<nc-palm_count;++i) {
            const auto& p=*candidates[i];
            if (p.kind==wb::Kind::Furniture) {
                if (!bench_surface || !matches_bench(p,surface,a,b,c)) continue;
            } else if (!matches_surface(p.kind,surface) || !matches_prop(p,a,b,c,x,z,vertical)) continue;
            discard[k]=changed=true;
            break;
        }
    }
    return changed;
}
int query(byte* t,const float* box,int maximum,byte** triangles,byte** vertices,uint8_t flags,void* scratch) {
    const int n=o_query(t,box,maximum,triangles,vertices,flags,scratch);
    g_queries.fetch_add(1,std::memory_order_relaxed);
    if (scratch || n<=0 || n>MAX_FILTER_TRIANGLES || n>maximum || !g_active_points.load(std::memory_order_relaxed)) return n;
    bool discard[MAX_FILTER_TRIANGLES]{};
    __try {
        if (!classify_triangles(t,box,n,triangles,vertices,discard)) return n;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return n; }
    int written=0;
    for (int k=0;k<n;++k) {
        if (discard[k]) continue;
        triangles[written]=triangles[k];
        vertices[written]=vertices[k];
        ++written;
    }
    g_removed.fetch_add(n-written,std::memory_order_relaxed);
    return written;
}
bool fence_blocked(byte* obj,wb::Kind kind) {
    if (!wb::is_barrier(kind) || !g_ground_check) return false;
    const float* position=reinterpret_cast<const float*>(obj+0xA0);
    Point* points[64];
    int n=nearby(position[0]-2,position[2]-2,position[0]+2,position[2]+2,points,64,wb::kind_bit(kind));
    Point* best=nullptr;float distance=1e9f;
    for (int i=0;i<n;++i) if (points[i]->radius<0) {
        float x=points[i]->x-position[0],z=points[i]->z-position[2],d=x*x+z*z;
        if(d<distance) { distance=d;best=points[i]; }
    }
    return best && best->ground.load()!=3;
}
std::atomic<uint64_t> g_slowdowns{};
std::atomic<uint64_t> g_unresolved_impacts{};
bool custom_kind(uint32_t id,wb::Kind* kind,float* size) {
    id&=0xFFFF;*size=1;
    if (id>=TREE_FIRST && id<TREE_FIRST+TREE_TIERS) { *kind=wb::Kind::Tree;*size=std::pow(2.0f,(int(id-TREE_FIRST)-4)/4.0f);return true; }
    if (id>=WOOD_FENCE && id<BARRIER_END) { *kind=barrier_kinds[(id-WOOD_FENCE)/3];return true; }
    if (id>=CLUTTER_FIRST && id<CLUTTER_END) {
        std::lock_guard<std::mutex> lock(g_alias_mutex);
        if (id-CLUTTER_FIRST>=g_clutter_count) return false;
        const auto& recipe=g_alias_recipes[id-CLUTTER_FIRST];
        *kind=recipe.kind;
        if (*kind==wb::Kind::Tree) *size=std::pow(2.0f,(int(recipe.tier)-4)/4.0f);
        return true;
    }
    return false;
}
float impact_size(const byte* object,wb::Kind kind,float fallback) {
    if (kind!=wb::Kind::Tree) return fallback;
    const auto pos=reinterpret_cast<const float*>(object+0xA0);
    if (!std::isfinite(pos[0]+pos[1]+pos[2])) return fallback;
    Point* points[64];
    const int n=nearby(pos[0]-1,pos[2]-1,pos[0]+1,pos[2]+1,points,64,wb::kind_bit(kind));
    float best=1.0f;
    for (int i=0;i<n;++i) {
        const auto p=points[i];
        const float x=p->x-pos[0],y=p->y-pos[1],z=p->z-pos[2];
        const float distance=x*x+y*y+z*z;
        if (distance<best && std::isfinite(p->tree_scale) && p->tree_scale>0) {
            best=distance;fallback=p->tree_scale;
        }
    }
    return fallback;
}
wb::Slowdown current_slowdown() {
    std::lock_guard<std::mutex> lock(g_slowdown_mutex);
    return g_slowdown;
}
bool apply_speed_loss(void* contact,wb::Kind kind,float size,const wb::Slowdown& slowdown) {
    const float loss=wb::speed_loss(kind,size,slowdown);
    if (loss<=0 || !readable(contact,16)) return false;
    auto other=*reinterpret_cast<byte**>(static_cast<byte*>(contact)+8);
    if (!other) return false;
    // Native FH3's own contact-to-ForzaCar adapter checks type1 and adjusts
    // its owner by -10. Neither the FH2 car fields nor its dynamics slots fit FH3.
    byte* car=nullptr;
    using CarFn=byte** (*)(byte**,byte*);
    reinterpret_cast<CarFn>(g_base+CAR_FROM_BODY)(&car,other);
    using GetFn=float* (*)(byte*,float*);using SetFn=void (*)(byte*,const float*);
    if (!car || !readable(car+0x71E8,8)) return false;
    auto dynamics=*reinterpret_cast<byte**>(car+0x71E8);
    if (!readable(dynamics,8)) return false;
    auto table=*reinterpret_cast<const byte**>(dynamics);
    // FH3 also allocates CCarDynamics in a deletion-tracked wrapper. Both
    // ICarDynamics tables have the same native GetVelocity/SetVelocity slots.
    if (table!=g_base+DYNAMICS_VTABLE && table!=g_base+TRACKED_DYNAMICS_VTABLE) return false;
    const auto get=*reinterpret_cast<const GetFn*>(table+0x328);
    const auto set=*reinterpret_cast<const SetFn*>(table+0x330);
    if (get!=reinterpret_cast<GetFn>(g_base+GET_VELOCITY) ||
        set!=reinterpret_cast<SetFn>(g_base+SET_VELOCITY)) return false;
    alignas(16) float velocity[4]{};
    get(dynamics,velocity);
    const float speed=std::hypot(velocity[0],velocity[1],velocity[2]);
    if (!std::isfinite(speed) || speed<=0) return false;
    for(int i=0;i<3;++i) velocity[i]*=1-loss;
    set(dynamics,velocity);
    const auto count=g_slowdowns.fetch_add(1,std::memory_order_relaxed);
    if (count<12 || g_debug)
        log("impact slowdown: category %u, size %.2f, %.1f%%, %.1f -> %.1f km/h",
            static_cast<unsigned>(kind),size,loss*100,speed*3.6f,speed*(1-loss)*3.6f);
    return true;
}
void collision(byte* obj,void* contact) {
    const int before=*reinterpret_cast<int32_t*>(obj+0x148);
    const auto id=*reinterpret_cast<const uint32_t*>(obj+0x128)&0xFFFF;
    wb::Kind kind=wb::Kind::Count;
    float size=1;
    if (before==0 && virtual_id(id)) {
        __try {
            if (custom_kind(id,&kind,&size)) {
                if (!(g_scenery_mask.load(std::memory_order_acquire)&wb::kind_bit(kind)) || fence_blocked(obj,kind)) return;
                size=impact_size(obj,kind,size);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    o_collision(obj,contact);
    if (kind!=wb::Kind::Count && *reinterpret_cast<int32_t*>(obj+0x148)!=0 && g_enabled.load(std::memory_order_acquire)) {
        g_hits.fetch_add(1,std::memory_order_relaxed);
        const auto slowdown=current_slowdown();
        if (wb::speed_loss(kind,size,slowdown)>0) {
            __try {
                if (!apply_speed_loss(contact,kind,size,slowdown) && g_unresolved_impacts.fetch_add(1)<6)
                    log("impact slowdown skipped: no moving car with a supported dynamics interface (definition %04X)",id);
            }
            __except(EXCEPTION_EXECUTE_HANDLER) { log("ERROR: slowdown rejected an unexpected contact/car layout"); }
        }
    }
}

struct DebrisAge { byte* object{};uint32_t definition{};byte key[16]{};double born{}; };
std::array<DebrisAge,4096> g_debris_age;
byte* g_debris_manager{};
void update_debris(byte* manager,double now) {
    if (!g_enabled.load(std::memory_order_acquire) || !g_debris_cleanup ||
        (g_debris_lifetime<=0 && g_debris_distance<=0)) return;
    if (manager!=g_debris_manager) { g_debris_age={};g_debris_manager=manager; }
    const auto slots=reinterpret_cast<const GameVector*>(manager+0x2A0);
    size_t count=slots->count(sizeof(byte*));
    if (count>g_debris_age.size() || (count && !readable(slots->first,count*sizeof(byte*)))) return;
    const auto player=reinterpret_cast<const float*>(manager+0x500);
    const int32_t default_key=*reinterpret_cast<const int32_t*>(g_base+0x42EA068);
    auto d=g_active_definitions.load(std::memory_order_acquire);
    const bool known_table=same_table(d,*reinterpret_cast<byte**>(manager+0x4D8));
    for (size_t i=0;i<count;++i) {
        auto& age=g_debris_age[i];
        auto object=reinterpret_cast<byte* const*>(slots->first)[i];
        if (!object || *reinterpret_cast<const int32_t*>(object+0x148)!=5 ||
            (*reinterpret_cast<const int32_t*>(object+0xCC)==default_key &&
             *reinterpret_cast<const int32_t*>(object+0xC8)==-1 && *reinterpret_cast<const int32_t*>(object+0xD0)==-1)) {
            age={};continue;
        }
        uint32_t definition=*reinterpret_cast<const uint32_t*>(object+0x128);
        uint32_t native=definition&0xFFFF;
        if (known_table && native<d->utility_poles.size() && d->utility_poles[native]) {
            age={};continue;
        }
        if (age.object!=object || age.definition!=definition || memcmp(age.key,object+0xC8,sizeof age.key)!=0) {
            age.object=object;age.definition=definition;age.born=now;memcpy(age.key,object+0xC8,sizeof age.key);continue;
        }
        // FH3 moved the half-float fade timer from FH2's +120 to +124.
        // Native 425B30 reads it, decrements it, and releases the body/model.
        auto fade=reinterpret_cast<uint16_t*>(object+0x124);
        if (*fade) continue;
        const auto position=reinterpret_cast<const float*>(object+0xA0);
        float dx=position[0]-player[0],dz=position[2]-player[2];
        bool old=g_debris_lifetime>0 && now-age.born>g_debris_lifetime;
        bool distant=g_debris_distance>0 && dx*dx+dz*dz>g_debris_distance*g_debris_distance;
        if (old || distant) { *fade=0x4000;g_debris_cleared.fetch_add(1,std::memory_order_relaxed); }
    }
}
const char* placed_name(int model) {
    auto first=std::begin(placed_model_names),last=std::end(placed_model_names);
    auto found=std::lower_bound(first,last,model,[](const PlacedModelName& a,int b){return a.id<b;});
    return found!=last && found->id==model?found->name:nullptr;
}
byte* track_presentation(byte* manager) {
    auto deferred=*reinterpret_cast<byte**>(manager+0x10);
    if (!readable(deferred,0x40)) return nullptr;
    const auto is_real=[](const byte* p) {
        if (!readable(p,0x40)) return false;
        auto vt=*reinterpret_cast<const uintptr_t*>(p)-reinterpret_cast<uintptr_t>(g_base);
        return vt==0x4684FA8 || vt==0x4689088;
    };
    byte* real=is_real(deferred)?deferred:nullptr;
    // FH3 stores this at the deferred root+30 (interface+20); FH2 used +38.
    for (size_t offset:{size_t(0x20),size_t(0x30),size_t(0x38)}) {
        if (real) break;
        auto candidate=*reinterpret_cast<byte**>(deferred+offset);
        if (is_real(candidate)) real=candidate;
    }
    if (!real) return nullptr;
    auto track=*reinterpret_cast<byte**>(real+0x38);
    return readable(track+0xFB28,0x60)?track:nullptr;
}
bool model_resident(const GameVector& models,int index) {
    if (index<0 || static_cast<size_t>(index)>=models.count(sizeof(byte*))) return false;
    const auto entry=reinterpret_cast<byte* const*>(models.first)+index;
    if (!readable(entry,sizeof *entry) || !readable(*entry,16)) return false;
    // CTrackPresentation uses this model vector for loose-piece binding.
    // The render model's native unload clears its resource pointer at +8.
    return *reinterpret_cast<byte* const*>(*entry+8)!=nullptr;
}
int resident_bench_model(byte* manager) {
    const auto track=track_presentation(manager);
    if (!track || !readable(track+0xF6F8,sizeof(GameVector))) return -1;
    const auto& models=*reinterpret_cast<const GameVector*>(track+0xF6F8);
    for (const auto index:BENCH_MODELS)
        if (model_resident(models,index)) return index;
    return -1;
}
bool resident_fragments(const GameVector& models,const byte* root) {
    const auto count=*reinterpret_cast<const unsigned*>(root+0x94);
    if (count<2 || count>MAX_FRAGMENTS) return false;
    const auto pieces=*reinterpret_cast<byte** const*>(root+0x88);
    if (!readable(pieces,count*sizeof *pieces)) return false;
    for (unsigned i=0;i<count;++i) {
        if (!readable(pieces[i],DEFINITION_SIZE) || !model_resident(models,*reinterpret_cast<const int*>(pieces[i]+0xC0)))
            return false;
    }
    return true;
}
const NativeDefinition* resident_donor(const Definitions& defs,const GameVector& models,
                                      std::initializer_list<const char*> names) {
    for (auto name:names) {
        auto item=exact_definition(&defs,name);
        if (item && resident_fragments(models,item->value)) return item;
    }
    return nullptr;
}
const NativeDefinition* fracture_donor(const Definitions& defs,const GameVector& models,
                                       const NativeDefinition& primary,const char* source,wb::Kind kind) {
    if (kind!=wb::Kind::Tree) return resident_fragments(models,primary.value)?&primary:nullptr;
    if (!source) {
        if (resident_fragments(models,primary.value)) return &primary;
        source=primary.name.c_str();
    }
    // These are authored breakable variants of the source tree family.
    // Prefer them over any fragment set supplied by a standing collision body.
    if (contains(source,"palm")) return resident_donor(defs,models,{"tree_global_palm_02"});
    if (contains(source,"pine")) return resident_donor(defs,models,{"tree_global_pine_01"});
    if (contains(source,"leopard")) {
        if (auto match=resident_donor(defs,models,{"tree_global_leopard_02"})) return match;
    }
    if (contains(source,"ghostgum")) {
        if (auto match=resident_donor(defs,models,{"tree_gorge_ghostgum_04","tree_gorge_ghostgum_06",
                                                  "tree_gorge_ghostgum_04_smashable"})) return match;
    }
    if (contains(source,"treefern")) {
        if (auto match=resident_donor(defs,models,{"tree_rainforest_treefern_01_smashable_small",
                                                  "tree_rainforest_treefern_02_smashable_small"})) return match;
    }
    if (contains(source,"gum")) {
        if (auto match=resident_donor(defs,models,{"tree_global_gum_06"})) return match;
    }
    if (resident_fragments(models,primary.value)) return &primary;
    return resident_donor(defs,models,{"tree_gorge_ghostgum_04","tree_gorge_ghostgum_06","tree_global_leopard_02",
        "tree_global_gum_06","tree_coast_sheoak_01","tree_coast_sheoak_02","tree_dead_tree_branch01"});
}
bool mark_prop_broken(const byte* object,uint32_t definition,wb::Kind kind) {
    // Native spawn saves the standing origin at E0. It survives impact
    // movement and matches the PGEO position because this hull's offset is zero.
    const auto origin=reinterpret_cast<const float*>(object+0xE0);
    if (!std::isfinite(origin[0]+origin[1]+origin[2]) || std::fabs(origin[0])>100000 || std::fabs(origin[2])>100000) return false;
    Point* points[32];
    const int n=nearby(origin[0]-0.2f,origin[2]-0.2f,origin[0]+0.2f,origin[2]+0.2f,points,32,wb::kind_bit(kind));
    for (int i=0;i<n;++i) {
        auto& point=*points[i];
        if (point.break_definition!=definition || std::fabs(point.x-origin[0])>0.2f ||
            std::fabs(point.y-origin[1])>0.3f || std::fabs(point.z-origin[2])>0.2f) continue;
        point.broken.store(true,std::memory_order_release);
        return true;
    }
    return false;
}
void release_broken_root(byte* manager,byte* object) {
    if ((*reinterpret_cast<const int32_t*>(object+0x148)&~4)==0) {
        byte key[20]{};
        memcpy(key,object+0xB8,16);
        reinterpret_cast<void (*)(byte*,const byte*)>(g_base+RECORD_BROKEN_KEY)(manager+0x560,key);
    }
    reinterpret_cast<ReleaseObjectFn>(g_base+RELEASE_OBJECT)(manager,object,0);
}
void break_pieces(byte* manager,byte* object) {
    const uint32_t id=*reinterpret_cast<const uint32_t*>(object+0x128);
    const auto low=id&0xFFFF;
    wb::Kind kind{};
    float size{};
    auto table=*reinterpret_cast<byte**>(manager+0x4D8);
    const auto defs=g_active_definitions.load(std::memory_order_acquire);
    if (id>>16!=0xFFFF || low<CLUTTER_FIRST || low>=CLUTTER_END ||
        !same_table(defs,table) || !defs->australia || !custom_kind(low,&kind,&size) ||
        (kind!=wb::Kind::Tree && kind!=wb::Kind::Furniture && !wb::is_barrier(kind))) {
        o_break_pieces(manager,object);
        return;
    }
    const auto standing=defs->bindings[low-TREE_FIRST].load(std::memory_order_acquire);
    if (!standing || !standing->definition || standing->loose_visual==LooseVisual::Fragments) {
        o_break_pieces(manager,object);
        return;
    }
    const bool bench=kind==wb::Kind::Furniture;
    if ((bench || standing->large_palm) && !mark_prop_broken(object,low,kind) && g_debug)
        log("fracture %04X: placement did not match its standing origin",low);
    if (bench) {
        if (resident_bench_model(manager)>=0) {
            o_break_pieces(manager,object);
        } else {
            static std::atomic<unsigned> bench_misses{};
            if (bench_misses.fetch_add(1,std::memory_order_relaxed)<12 || g_debug)
                log("bench debris %04X: no original bench detail level resident",low);
            release_broken_root(manager,object);
        }
        return;
    }
    if (kind!=wb::Kind::Tree && standing->loose_visual==LooseVisual::OriginalProp) {
        // Preserve the intact source prop until there is a verified multipart
        // render asset for it. Its standing donor can be a different object.
        o_break_pieces(manager,object);
        return;
    }
    const char* source=nullptr;
    if (kind==wb::Kind::Tree) {
        const int model_index=*reinterpret_cast<const int*>(standing->definition+0xC0);
        for (const auto& model:australia_scenery_models) {
            if (model.index==model_index) { source=model.name;break; }
        }
    }
    const NativeDefinition* donor=nullptr;
    if (const auto track=track_presentation(manager); track && readable(track+0xF6F8,sizeof(GameVector))) {
        const auto& models=*reinterpret_cast<const GameVector*>(track+0xF6F8);
        for (const auto& item:defs->items) {
            if (item.id!=standing->native) continue;
            donor=fracture_donor(*defs,models,item,source,kind);
            break;
        }
    }
    uint16_t native{};
    const auto alias=donor?scenery_alias(donor->name,kind,0,-1,4,true):0;
    auto definition=alias?resolve_definition(table,alias,&native):nullptr;
    if (!definition) {
        if (kind!=wb::Kind::Tree) { o_break_pieces(manager,object);return; }
        // Match the stock break's bookkeeping and release the standing body
        // when no complete visual set is resident. Never spawn invisible hulls.
        static std::atomic<unsigned> misses{};
        if (misses.fetch_add(1,std::memory_order_relaxed)<12 || g_debug)
            log("fracture %04X: no complete resident piece set",low);
        release_broken_root(manager,object);
        return;
    }
    alignas(16) byte aligned_root[DEFINITION_SIZE];
    memcpy(aligned_root,definition,DEFINITION_SIZE);
    // Use the struck object's model origin for the piece transforms.
    memcpy(aligned_root+0x68,standing->definition+0x68,3*sizeof(float));
    const auto previous_source=g_fracture_source;
    const auto previous_lookup=g_loose_visual;
    g_fracture_source={object,alias};
    g_loose_visual={table,0xFFFF0000|alias,aligned_root};
    static std::atomic<unsigned> reported{};
    if (reported.fetch_add(1,std::memory_order_relaxed)<12 || g_debug)
        log("fracture %04X: %s, %u collidable pieces",low,donor->name.c_str(),*reinterpret_cast<const unsigned*>(definition+0x94));
    __try { o_break_pieces(manager,object); }
    __finally { g_loose_visual=previous_lookup;g_fracture_source=previous_source; }
}
void report_nearby(byte* manager) {
    const auto player=reinterpret_cast<const float*>(manager+0x500);
    log("F8 scenery probe: player %.2f %.2f %.2f",player[0],player[1],player[2]);
    auto cells=reinterpret_cast<const GameVector*>(manager+0x548);
    size_t count=cells->count(0x40);
    unsigned shown=0;
    if (count<=8192 && (!count || readable(cells->first,count*0x40))) for (size_t c=0;c<count;++c) {
        auto cell_data=cells->first+c*0x40;
        const auto bounds=reinterpret_cast<const float*>(cell_data);
        if (bounds[0]>player[0]+25 || bounds[4]<player[0]-25 || bounds[2]>player[2]+25 || bounds[6]<player[2]-25) continue;
        auto provider=*reinterpret_cast<byte**>(cell_data+0x20);
        if (!readable(provider,0x1D0) || *reinterpret_cast<uintptr_t*>(provider)!=reinterpret_cast<uintptr_t>(g_base)+TREE_PROVIDER_VTABLE) continue;
        auto data=*reinterpret_cast<byte**>(provider+0x1C8);
        if (!readable(data,0x100)) continue;
        auto batches=reinterpret_cast<const GameVector*>(data+0xA0);
        auto groups=reinterpret_cast<const GameVector*>(data+0x60);
        size_t nb=batches->count(MODEL_BATCH_SIZE),ng=groups->count(sizeof(Group));
        if (nb>4096 || ng>65535 || !readable(batches->first,nb*MODEL_BATCH_SIZE) || (ng && !readable(groups->first,ng*sizeof(Group)))) continue;
        for (size_t b=0;b<nb;++b) {
            auto batch=batches->first+b*MODEL_BATCH_SIZE;
            auto name=string_at(batch+MODEL_BATCH_NAME);
            if (!name || !contains(name,"tree")) continue;
            auto instances=reinterpret_cast<const GameVector*>(batch);
            size_t ni=instances->count(0x20);
            if (ni>65535 || !readable(instances->first,ni*0x20)) continue;
            for (size_t i=0;i<ni;++i) {
                alignas(16) float matrix[16]{};float scale=1;
                reinterpret_cast<DecodeFn>(g_base+DECODE_INSTANCE)(data,matrix,instances->first+i*0x20,&scale);
                float dx=matrix[12]-player[0],dz=matrix[14]-player[2];
                if (dx*dx+dz*dz>25*25 || shown>=48) continue;
                int definition=-1;
                for (size_t j=0;j<ng;++j) {
                    const auto& group=reinterpret_cast<const Group*>(groups->first)[j];
                    if (group.model==int(b) && int(i)>=group.start && int(i)<group.start+group.count) { definition=group.definition;break; }
                }
                Point* points[64];int near_count=nearby(matrix[12]-1,matrix[14]-1,matrix[12]+1,matrix[14]+1,points,64,wb::all_kinds);
                log("probe procedural: %s, at %.2f %.2f %.2f, scale %.3f, definition %X, points %d, cell children %u",name,matrix[12],matrix[13],matrix[14],scale,definition,near_count,*reinterpret_cast<const uint16_t*>(cell_data+0x30));
                ++shown;
            }
        }
    }
    log("probe: %zu registered cells, %u nearby procedural trees",count,shown);
    auto track=track_presentation(manager);
    if (!track) { log("probe: real track presentation unavailable");return; }
    for (size_t offset:{size_t(0xFB28),size_t(0xFB70)}) {
        auto rows=reinterpret_cast<const GameVector*>(track+offset);
        size_t n=rows->count(0x70);unsigned near_count=0;
        if (n>500000 || !readable(rows->first,n*0x70)) { log("probe: track vector +%zX unavailable",offset);continue; }
        for (size_t i=0;i<n && near_count<32;++i) {
            auto row=rows->first+i*0x70;
            auto name=placed_name(*reinterpret_cast<const int32_t*>(row+0x10));
            if (!name) continue;
            const auto p=reinterpret_cast<const float*>(row+0x38);
            float dx=p[0]-player[0],dz=p[2]-player[2];
            if (dx*dx+dz*dz>25*25) continue;
            log("probe placed: %s, index %zu, at %.2f %.2f %.2f, vector +%zX",name,i,p[0],p[1],p[2],offset);++near_count;
        }
        log("probe: track +%zX has %zu rows, %u nearby trees/poles",offset,n,near_count);
    }
}
void update_status();
void world_update(byte* manager,const float* position,float a,float b) {
    o_world_update(manager,position,a,b);
#ifdef WB_MOD_PLATFORM
    // The platform does not call ExtraOptions' per-frame export.
    update_status();
#endif
    static unsigned frame{};
    if (++frame%10==0) {
        __try { update_debris(manager,GetTickCount64()/1000.0); }
        __except(EXCEPTION_EXECUTE_HANDLER) { log("ERROR: debris update rejected an unexpected layout");g_debris_cleanup=false; }
    }
    if (g_probe_requested.exchange(false,std::memory_order_acq_rel)) {
        __try { report_nearby(manager); }
        __except(EXCEPTION_EXECUTE_HANDLER) { log("ERROR: scenery probe rejected an unexpected layout"); }
    }
}
struct Check { uint32_t rva; const char* hex; };
bool bytes_match(uint32_t rva,const char* hex) {
    for (size_t i=0;hex[i];i+=2) {
        unsigned n; if (sscanf_s(hex+i,"%2x",&n)!=1 || g_base[rva+i/2]!=n) return false;
    }
    return true;
}
bool verify() {
    if (!readable(g_base,0x1000) || *reinterpret_cast<uint16_t*>(g_base)!=IMAGE_DOS_SIGNATURE) return false;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(g_base);
    if (dos->e_lfanew<0 || dos->e_lfanew>0xC00) return false;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt->FileHeader.TimeDateStamp!=TIMESTAMP || nt->OptionalHeader.SizeOfImage!=IMAGE_SIZE) return false;
    const Check checks[]={
        {SERIALIZE,"40574883EC5048C7442420FEFFFFFF"},
        {PROCEDURAL_DESTRUCTOR,"48895C24104889742418574883EC20"},
        {RESIZE_GROUPS,"48894C2408574883EC30"},
        {DECODE_INSTANCE,"4881EC88000000C4C17B1200"},
        {DEF_LOOKUP,"488B4108B9FFFF0000440FB7C2C1EA10"},
        {DEF_MODEL,"488B4108440FB7C2C1EA10"},
        {TEMPLATE_QUERY,"48895C24205657415441554156"},
        {WORLD_TEMPLATE_CALL,"488D8AD800000048C74424300000000044886C24284C897424204D8BCF458BC4488BD6E80AF6FFFF"},
        {TEMPLATE_DESERIALIZE,"405556574154415541564157488BEC"},
        {BIND_LOOSE_MODEL,"40555657415641574881EC80000000"},
        {BREAK_PIECES,"488BC4488950104889480855535657"},
        {OBJECT_DEFINITION_ID,"8B81280100008902488BC2C3"},
        {FRACTURE_ROOT_CALL,"488B02488D55E0498BCDFF5040"},
        {FRACTURE_CHILD_CALL,"498B4500488D95C8010000498BCDFF5040"},
        {RELEASE_OBJECT,"48895C2410488974241848897C2420"},
        {RECORD_BROKEN_KEY,"48895C2408574883EC20488BD9488B"},
        {TRACK_MODEL_LOOKUP,"488B86F8F600004A8B2CF8"},
        {TRACK_MODEL_UNLOAD,"48C7470800000000"},
        {ON_COLLISION,"48895C24084889742410574883EC20"},
        {TABLE_CONSTRUCTOR,"488D05B908970248890133C048894108"},
        {TABLE_DESTRUCTOR,"48895C2408574883EC20488D059F6D96"},
        {MODEL_BIND,"48895C24188954241055565741544155"},
        {ADD_CELL,"4055565741564157488D6C24C94881ECA0000000"},
        {WORLD_UPDATE,"48895C24084889742410574883EC40"},
        {CAR_FROM_BODY,"48895C24084889742410574883EC20"},
        {GET_VELOCITY,"C5F8108160FBFFFFC5F81102488BC2C3"},
        {SET_VELOCITY,"C5F81002C4E37940C87FC5F851D1C5FA1191D4120000C5F8118160FBFFFFC3"},
        {0x425B30,"0FB78324010000C5F96EC0"}
    };
    for (auto& c:checks) if (!bytes_match(c.rva,c.hex)) { log("REFUSED: FH3 code check failed at RVA %08X",c.rva);return false; }
    return true;
}
bool create_hook(uintptr_t rva, const char* name, void* detour, void** original) {
#ifdef WB_MOD_PLATFORM
    return g_platform->hook_function(g_mod, name, g_base + rva, detour, original) != nullptr;
#else
    auto result=MH_CreateHook(g_base+rva,detour,original);
    if (result!=MH_OK) log("ERROR: hook %s at %08llX: %s",name,static_cast<unsigned long long>(rva),MH_StatusToString(result));
    return result==MH_OK;
#endif
}
std::string ini_text(const std::wstring& ini,const wchar_t* key,const wchar_t* fallback) {
    wchar_t value[2048];GetPrivateProfileStringW(L"WreckingBall",key,fallback,value,2048,ini.c_str());
    int bytes=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    std::string result(bytes>0?bytes:1,'\0');
    WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),int(result.size()),nullptr,nullptr);
    result.resize(strlen(result.c_str()));return result;
}
float ini_float(const std::wstring& ini,const wchar_t* key,float fallback,float low,float high) {
    auto value=ini_text(ini,key,L"");char* end{};float parsed=std::strtof(value.c_str(),&end);
    return end==value.c_str() || *end || !std::isfinite(parsed)?fallback:std::clamp(parsed,low,high);
}
void load_settings(const std::wstring& ini) {
    auto flag=[&](const wchar_t* key,bool fallback){return GetPrivateProfileIntW(L"WreckingBall",key,fallback?1:0,ini.c_str())!=0;};
    g_rules=wb::Rules{};
    g_rules.enabled=flag(L"Enabled",true);
    g_rules.trees=flag(L"InjectTrees",true);
    g_rules.barriers=flag(L"Barriers",true);
    g_rules.skip_events=flag(L"BarrierSkipEvents",true);
    const std::pair<wb::Kind,const wchar_t*> categories[]={
        {wb::Kind::Fence,L"Fences"},{wb::Kind::Railing,L"Railings"},
        {wb::Kind::Guardrail,L"Guardrails"},{wb::Kind::Bollard,L"Bollards"},
        {wb::Kind::Furniture,L"ConcreteBenches"}
    };
    for (auto category:categories) g_rules.categories[static_cast<size_t>(category.first)]=flag(category.second,true);
    g_rules.prefix=ini_text(ini,L"Prefix",L"*");g_rules.skip_models=ini_text(ini,L"SkipModels",L"ROOT_");
    g_rules.barrier_exclude=ini_text(ini,L"BarrierExclude",L"Crowd,FEST_,Carmeet,Banner,Festival,Concrete,Blast,Wall,Bridge,Gate,Hydraulic,Tyre,Tire");
    g_rules.exclude_libraries=ini_text(ini,L"ExcludeLibraries",L"Festival,Crowd,Carmeet,Building,Bridge,Railway,Train,Checkpoint,Grandstand");
    g_rules.size_rules=ini_text(ini,L"SizeRules",L"Skinny:0.8,Palm:0.9,Pine:1.3,Tall:1.3");
    g_slowdown.tree=ini_float(ini,L"SpeedLoss",0.12f,0,0.95f);
    g_slowdown.maximum=ini_float(ini,L"SpeedLossMax",0.45f,0,0.95f);
    g_slowdown.exponent=ini_float(ini,L"SpeedLossExponent",1.5f,0,4);
    g_slowdown.barrier=ini_float(ini,L"BarrierSpeedLoss",0.04f,0,0.95f);
    g_big_tree_size=ini_float(ini,L"LumberjackMinSize",1.6f,0.1f,20);
    g_trunk_radius=ini_float(ini,L"TrunkRadius",1.6f,0.1f,5);
    g_barrier_radius=ini_float(ini,L"BarrierPointRadius",0.9f,0.1f,3);
    g_debug=flag(L"Debug",false);
    g_ground_check=flag(L"BarrierNeedGround",flag(L"BarrierNeedsGround",true));
    g_debris_cleanup=flag(L"DebrisCleanup",true);
    g_debris_lifetime=ini_float(ini,L"DebrisLifetime",45,0,3600);
    g_debris_distance=ini_float(ini,L"DebrisDistance",120,0,5000);
    publish_scenery();
    log("settings: trees %d, barriers %d, guardrails %d; loss tree %.3f, max %.3f, exponent %.2f, barrier %.3f",g_rules.trees,g_rules.barriers,g_rules.categories[static_cast<size_t>(wb::Kind::Guardrail)],g_slowdown.tree,g_slowdown.maximum,g_slowdown.exponent,g_slowdown.barrier);
}
void seed_settings(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES) return;
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&g_directory),&module)) return;
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));
    if (!resource) return;
    const DWORD size=SizeofResource(module,resource);
    const void* bytes=LockResource(LoadResource(module,resource));
    if (!size || !bytes) return;
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        if (error!=ERROR_FILE_EXISTS) log("ERROR: could not create WreckingBall.ini (Windows error %lu)",error);
        return;
    }
    DWORD written{};
    const bool ok=WriteFile(file,bytes,size,&written,nullptr) && written==size;
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
        log("ERROR: could not write the default WreckingBall.ini");
    }
}
bool read_config(const char* loader, const char* loader_version) {
    if (g_log) fclose(g_log);
    g_log = _wfsopen((g_directory + L"\\WreckingBall.log").c_str(), L"w", _SH_DENYNO);
    log("WreckingBall FH3 %s (collision safety); %s %s", VERSION, loader, loader_version ? loader_version : "?");
    const auto ini = g_directory + L"\\WreckingBall.ini";
    seed_settings(ini);
    load_settings(ini);
    if (g_rules.enabled) return true;
    host_log("WreckingBall FH3 is disabled in its INI.");
    return false;
}
#ifdef WB_MOD_PLATFORM
bool save_setting(const char* key, const wchar_t* value) {
    const std::wstring wide_key(key, key + strlen(key));
    const auto ini = g_directory + L"\\WreckingBall.ini";
    if (WritePrivateProfileStringW(L"WreckingBall", wide_key.c_str(), value, ini.c_str())) return true;
    log("ERROR: could not save menu setting %s (Windows error %lu)", key, GetLastError());
    return false;
}
void change_switch(void* user, const char* key, int value) {
    if (!save_setting(key, value ? L"1" : L"0")) return;
    std::lock_guard<std::mutex> lock(g_rules_mutex);
    *static_cast<bool*>(user) = value != 0;
    publish_scenery();
    log("live scenery: %s %s; new conversions apply as areas stream in", key, value ? "on" : "off");
}
void change_debris_cleanup(void*, const char* key, int value) {
    if (save_setting(key, value ? L"1" : L"0")) g_debris_cleanup = value != 0;
}

struct SlowdownSlider {
    const char* key;
    const char* label;
    const char* description;
    float wb::Slowdown::*field;
    float min, max, step, display_scale;
    int decimals;
};
constexpr SlowdownSlider slowdown_sliders[] = {
    {"SpeedLoss", "Tree slowdown", "Speed taken by a converted normal-size tree. Biggest tree slowdown caps the result.",
        &wb::Slowdown::tree, 0, 0.95f, 0.01f, 100, 0},
    {"SpeedLossMax", "Biggest tree slowdown", "Maximum extra speed loss, in percent. Set to 0 to turn off all extra slowdown.",
        &wb::Slowdown::maximum, 0, 0.95f, 0.01f, 100, 0},
    {"SpeedLossExponent", "Tree size effect", "How strongly tree size affects slowdown. At 0, every tree uses the same base slowdown.",
        &wb::Slowdown::exponent, 0, 4, 0.1f, 1, 1},
    {"BarrierSpeedLoss", "Fence slowdown", "Extra speed loss on converted fences, railings, guardrails and bollards, in percent.",
        &wb::Slowdown::barrier, 0, 0.95f, 0.01f, 100, 0}
};
void change_slowdown(void* user, const char* key, float value) {
    const auto& slider = *static_cast<const SlowdownSlider*>(user);
    if (!std::isfinite(value)) return;
    value = std::clamp(value, slider.min, slider.max);
    // Round away the float error from repeated presses of the game's slider.
    const int precision = slider.display_scale == 100 ? 2 : 1;
    const float scale = precision == 2 ? 100.0f : 10.0f;
    value = std::round(value * scale) / scale;
    if (value < 0) value = -1;
    char ascii[32];
    const auto result = std::to_chars(ascii, ascii + sizeof ascii, value, std::chars_format::fixed, precision);
    if (result.ec != std::errc{}) return;
    const std::wstring text(ascii, result.ptr);
    if (save_setting(key, text.c_str())) {
        std::lock_guard<std::mutex> lock(g_slowdown_mutex);
        g_slowdown.*slider.field = value;
    }
}
void add_menu_setting(FH3Setting row, const char* description_tag) {
    std::string description;
    if (description_tag || row.on_change == change_switch) {
        description = row.description;
        if (row.on_change == change_switch)
            description += " Off stops extra breaks now. New conversions appear as areas load.";
        if (description_tag) description += description_tag;
        row.description = description.c_str();
    }
    if (!g_platform->add_setting(g_mod, &row)) log("ERROR: could not register menu setting %s", row.key);
}
void register_settings(const char* description_tag) {
    auto toggle = [description_tag](const char* key, const char* label, const char* description, bool& value) {
        FH3Setting row{};
        row.size = sizeof(row);
        row.key = key;
        row.label = label;
        row.description = description;
        row.value = value;
        row.on_change = change_switch;
        row.user = &value;
        add_menu_setting(row, description_tag);
    };
    auto category = [](wb::Kind kind) -> bool& { return g_rules.categories[static_cast<size_t>(kind)]; };
    toggle("Enabled", "Enable WreckingBall", "Turn on WreckingBall's scenery and collision changes.", g_rules.enabled);
    toggle("InjectTrees", "Breakable trees", "Make eligible trees breakable.", g_rules.trees);
    toggle("Barriers", "Breakable barriers", "Enable the fence, railing, guardrail and bollard categories.", g_rules.barriers);
    toggle("Fences", "Fences", "Requires Breakable barriers.", category(wb::Kind::Fence));
    toggle("Railings", "Railings", "Requires Breakable barriers.", category(wb::Kind::Railing));
    toggle("Guardrails", "Armco guardrails", "Convert solid wooden and metal Armco guardrails. Requires Breakable barriers.", category(wb::Kind::Guardrail));
    toggle("Bollards", "Bollards", "Requires Breakable barriers.", category(wb::Kind::Bollard));
    toggle("ConcreteBenches", "Breakable benches", "Convert solid benches in mainland Australia. The game already handles other bench types.", category(wb::Kind::Furniture));
    FH3Setting debris{};
    debris.size = sizeof(debris);
    debris.key = "DebrisCleanup";
    debris.label = "Debris clean-up";
    debris.description = "Fade eligible debris after enough time or distance.";
    debris.value = g_debris_cleanup;
    debris.on_change = change_debris_cleanup;
    add_menu_setting(debris, description_tag);

    const auto slowdown = current_slowdown();
    for (const auto& slider : slowdown_sliders) {
        FH3Setting row{};
        row.size = sizeof(row);
        row.key = slider.key;
        row.label = slider.label;
        row.description = slider.description;
        row.kind = FH3_SETTING_SLIDER;
        row.min = slider.min;
        row.max = slider.max;
        row.step = slider.step;
        const float value = slowdown.*slider.field;
        row.slider_value = value < 0 ? row.min : value;
        row.display_scale = slider.display_scale;
        row.decimals = slider.decimals;
        row.on_slide = change_slowdown;
        row.user = const_cast<SlowdownSlider*>(&slider);
        add_menu_setting(row, description_tag);
    }
}
#endif
bool install_hooks(byte* image) {
    g_base = image;
    if (!verify()) {
        host_log("WreckingBall FH3 startup refused because the game code did not match.");
        return false;
    }
    g_points=static_cast<Point*>(VirtualAlloc(nullptr,sizeof(Point)*POINT_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!g_points) {
        host_log("WreckingBall FH3 could not allocate collision points.");
        return false;
    }
    for(auto& h:g_heads) h.store(-1);
#ifndef WB_MOD_PLATFORM
    if (MH_Initialize() != MH_OK) {
        host_log("WreckingBall FH3 hook engine initialization failed.");
        return false;
    }
#endif
    const bool registered =
        create_hook(SERIALIZE, "Procedural scenery serialization", reinterpret_cast<void*>(serialize), reinterpret_cast<void**>(&o_serialize)) &&
        create_hook(PROCEDURAL_DESTRUCTOR, "Procedural scenery cleanup", reinterpret_cast<void*>(procedural_destructor), reinterpret_cast<void**>(&o_procedural_destructor)) &&
        create_hook(DEF_LOOKUP, "Breakable definition lookup", reinterpret_cast<void*>(lookup), reinterpret_cast<void**>(&o_lookup)) &&
        create_hook(DEF_MODEL, "Breakable model lookup", reinterpret_cast<void*>(model), reinterpret_cast<void**>(&o_model)) &&
        create_hook(TEMPLATE_QUERY, "Scenery collision query", reinterpret_cast<void*>(query), reinterpret_cast<void**>(&o_query)) &&
        create_hook(BIND_LOOSE_MODEL, "Loose scenery model binding", reinterpret_cast<void*>(bind_loose_model), reinterpret_cast<void**>(&o_bind_loose_model)) &&
        create_hook(OBJECT_DEFINITION_ID, "Scenery fracture piece IDs", reinterpret_cast<void*>(object_definition_id), reinterpret_cast<void**>(&o_object_definition_id)) &&
        create_hook(BREAK_PIECES, "Collidable scenery fracture pieces", reinterpret_cast<void*>(break_pieces), reinterpret_cast<void**>(&o_break_pieces)) &&
        create_hook(ON_COLLISION, "Scenery break and slowdown", reinterpret_cast<void*>(collision), reinterpret_cast<void**>(&o_collision)) &&
        create_hook(TABLE_CONSTRUCTOR, "Breakable definition table creation", reinterpret_cast<void*>(table_constructor), reinterpret_cast<void**>(&o_table_constructor)) &&
        create_hook(TABLE_DESTRUCTOR, "Breakable definition table cleanup", reinterpret_cast<void*>(table_destructor), reinterpret_cast<void**>(&o_table_destructor)) &&
        create_hook(MODEL_BIND, "Procedural model binding", reinterpret_cast<void*>(model_bind), reinterpret_cast<void**>(&o_model_bind)) &&
        create_hook(ADD_CELL, "Procedural cell registration", reinterpret_cast<void*>(add_cell), reinterpret_cast<void**>(&o_add_cell)) &&
        create_hook(WORLD_UPDATE, "World collision update", reinterpret_cast<void*>(world_update), reinterpret_cast<void**>(&o_world_update));
#ifdef WB_MOD_PLATFORM
    if (!registered || !g_platform->enable_hooks(g_mod, nullptr)) {
        g_platform->disable_hooks(g_mod, nullptr);
        host_log("WreckingBall FH3 hook setup failed. See fh3-mods\\platform.log.");
        return false;
    }
#else
    if (!registered || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_Uninitialize();
        host_log("WreckingBall FH3 hook setup failed.");
        return false;
    }
#endif
    g_ready.store(true, std::memory_order_release);
    host_log("WreckingBall FH3 hooks are ready.");
    return true;
}
void update_status() {
    if (!g_ready.load(std::memory_order_acquire)) return;
    static bool f8_down{};
    bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if (down && !f8_down) g_probe_requested.store(true,std::memory_order_release);
    f8_down=down;
    bootstrap_definitions();
    static ULONGLONG last{};ULONGLONG now=GetTickCount64();
    if (now-last<15000) return;last=now;
    log("stats: %llu procedurals, %llu points, %llu mesh queries, %llu triangles removed, %llu alias misses; scans %llu, cache hits %llu, early table deferrals %llu, model retries %llu, cell retries %llu, clutter points %llu, debris faded %llu, breaks %llu, slowdowns %llu",g_procedurals.load(),static_cast<uint64_t>(g_point_count.load()),g_queries.load(),g_removed.load(),g_alias_misses.load(),g_definition_scans.load(),g_definition_cache_hits.load(),g_unready_skips.load(),g_late_passes.load(),g_cell_retries.load(),g_clutter_instances.load(),g_debris_cleared.load(),g_hits.load(),g_slowdowns.load());
}

#ifdef WB_MOD_PLATFORM
#include "platform_menu.h"
DWORD WINAPI platform_start(void*) {
    auto* image = static_cast<byte*>(g_platform->wait_for_code(INFINITE));
    if (!image) {
        host_log("WreckingBall FH3 could not obtain the mapped game image.");
        return 1;
    }
    const bool gameplay_ready = install_hooks(image);
    if (wb_menu::install(image)) host_log("WreckingBall settings preview and A confirmation are ready.");
    else host_log("WreckingBall settings confirmation unavailable on this Mod Platform build; using its standard controls.");
    return gameplay_ready ? 0 : 1;
}
#endif
}

#ifdef WB_MOD_PLATFORM
FH3MOD_EXPORT int fh3mod_init(const FH3ModApi* api) {
    constexpr size_t minimum_size = offsetof(FH3ModApi, add_setting) + sizeof(api->add_setting);
    if (!api || api->size < minimum_size || api->api_version < 9 || !api->azimuth_dir ||
        !api->register_mod || !api->log || !api->hook_function || !api->enable_hooks ||
        !api->disable_hooks || !api->wait_for_code || !api->add_setting) return 1;

    FH3ModInfo info{};
    info.size = sizeof(info);
    info.name = "WreckingBall";
    info.version = VERSION;
    info.platform_version = "1.0.0";
    info.description = "Make solid trees, fences, railings, guardrails, bollards and concrete benches breakable.";
    info.offline_only = 1;
    g_mod = api->register_mod(&info);
    if (!g_mod) return 1;

    g_platform = api;
    g_directory = std::wstring(api->azimuth_dir) + L"\\fh3-mods";
    read_config("FH3 Mod Platform", api->platform_version);
    register_settings(wb_menu::hide_setting_owner()?wb_menu::setting_tag:nullptr);
    HANDLE worker = CreateThread(nullptr, 0, platform_start, nullptr, 0, nullptr);
    if (!worker) {
        host_log("WreckingBall FH3 could not start its setup thread.");
        return 1;
    }
    CloseHandle(worker);
    return 0;
}
#else
extern "C" __declspec(dllexport) BOOL WINAPI FH3PluginInitialize(const FH3PluginContext* context) {
    if (!context || context->size < 64 || context->api_version != 1 || !context->game_image || !context->mod_directory) return FALSE;
    g_context = context;
    g_directory = context->mod_directory;
    if (!read_config("ExtraOptions", context->extra_options_version)) return FALSE;
    if (context->game_image_size != IMAGE_SIZE) {
        host_log("WreckingBall FH3 startup refused because the image size did not match.");
        return FALSE;
    }
    return install_hooks(static_cast<byte*>(context->game_image)) ? TRUE : FALSE;
}
extern "C" __declspec(dllexport) void WINAPI FH3PluginUpdate(const FH3PluginFrame*) {
    update_status();
}
#endif
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
