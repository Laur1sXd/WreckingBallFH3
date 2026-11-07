#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace wb {
enum class Kind : unsigned char { Tree,Fence,Railing,Guardrail,Sign,Bin,Streetlight,Bollard,Furniture,Pole,Other,Count };
inline constexpr size_t kind_count=static_cast<size_t>(Kind::Count);
inline std::string lower(std::string_view value) {
    std::string result(value);
    for(char& c:result) if(c>='A' && c<='Z') c+=32;
    return result;
}
inline std::string_view trim(std::string_view value) {
    while(!value.empty() && (value.front()==' ' || value.front()=='\t')) value.remove_prefix(1);
    while(!value.empty() && (value.back()==' ' || value.back()=='\t')) value.remove_suffix(1);
    return value;
}
inline bool list_match(std::string_view list,std::string_view value) {
    const auto name=lower(value);
    while(!list.empty()) {
        auto comma=list.find(',');auto token=trim(list.substr(0,comma));
        if(token=="*" || (!token.empty() && name.find(lower(token))!=std::string::npos)) return true;
        if(comma==std::string_view::npos) break;
        list.remove_prefix(comma+1);
    }
    return false;
}
inline bool prefix_match(std::string_view list,std::string_view value) {
    const auto name=lower(value);
    while(!list.empty()) {
        auto comma=list.find(',');auto token=trim(list.substr(0,comma));
        auto prefix=lower(token);
        if(token=="*" || (!token.empty() && name.compare(0,prefix.size(),prefix)==0)) return true;
        if(comma==std::string_view::npos) break;
        list.remove_prefix(comma+1);
    }
    return false;
}
inline float size_factor(std::string_view rules,std::string_view name) {
    while(!rules.empty()) {
        auto comma=rules.find(',');auto rule=trim(rules.substr(0,comma));auto colon=rule.find(':');
        if(colon!=std::string_view::npos && list_match(rule.substr(0,colon),name)) {
            auto text=std::string(rule.substr(colon+1));char* end{};
            float value=std::strtof(text.c_str(),&end);
            return end!=text.c_str() && std::isfinite(value) && value>0?std::clamp(value,0.05f,20.0f):1.0f;
        }
        if(comma==std::string_view::npos) break;
        rules.remove_prefix(comma+1);
    }
    return 1;
}
struct Rules {
    std::array<bool,kind_count> categories;
    bool enabled=true,trees=true,barriers=true,skip_events=true;
    std::string prefix="*",skip_models="ROOT_";
    std::string barrier_exclude="Crowd,FEST_,Carmeet,Banner,Festival,Concrete,Blast,Wall,Bridge,Gate,Hydraulic,Tyre,Tire";
    std::string exclude_libraries="Festival,Crowd,Carmeet,Building,Bridge,Railway,Train,Checkpoint,Grandstand";
    std::string size_rules="Skinny:0.8,Palm:0.9,Pine:1.3,Tall:1.3";
    Rules(){categories.fill(true);}
};
inline bool is_utility_pole(std::string_view name) {
    return list_match("pyln_,pylon_generic_wooden,pylon_nshaped_wooden,telephone_pole,telegraph_pole,tram_pylon",name);
}
inline bool is_solid_bench(std::string_view name) {
    const auto model=lower(name);
    return model=="prop_surfers_bench_02" || model=="prop_surfers_bench_02_3d";
}
inline Kind classify(std::string_view name,std::string_view library) {
    if(is_utility_pole(name)) return Kind::Pole;
    if(list_match("bushes,crops",library) || list_match("Bush_,Crop_,Crops_,Hedge_,VINEYARD_",name)) return Kind::Other;
    if(list_match("Tree_,Trees_",name) || list_match("trees",library)) return Kind::Tree;
    if(list_match("streetlight,street_light,lamppost,lamp_post",name)) return Kind::Streetlight;
    if(list_match("bollard",name)) return Kind::Bollard;
    if(list_match("barr_armco_,barr_plains_armco_,guardrail",name)) return Kind::Guardrail;
    if(list_match("fenc,barrier",name)) return Kind::Fence;
    if(list_match("railing,barr_rail,scooter_rail",name)) return Kind::Railing;
    if(list_match("sign_,billboard,sandwichboard,rewardboard",name)) return Kind::Sign;
    if(list_match("bin_,bins_,trash",name)) return Kind::Bin;
    if(list_match("bench,chair,table,parasol,umbrella,planter",name)) return Kind::Furniture;
    return Kind::Other;
}
inline constexpr bool is_barrier(Kind kind) {
    return kind==Kind::Fence || kind==Kind::Railing || kind==Kind::Guardrail || kind==Kind::Bollard;
}
inline bool category_enabled(Kind kind,const Rules& rules) {
    if(!rules.enabled || kind==Kind::Pole || !rules.categories[static_cast<size_t>(kind)]) return false;
    if(kind==Kind::Tree) return rules.trees;
    if(kind==Kind::Furniture) return true;
    return is_barrier(kind) && rules.barriers;
}
using SceneryMask=uint16_t;
static_assert(kind_count<=16);
inline constexpr auto all_kinds=static_cast<SceneryMask>((1u<<kind_count)-1);
inline constexpr SceneryMask kind_bit(Kind kind) { return SceneryMask(1u << static_cast<unsigned>(kind)); }
inline SceneryMask scenery_mask(const Rules& rules) {
    SceneryMask mask=0;
    for(size_t i=0;i<kind_count;++i) {
        const auto kind=static_cast<Kind>(i);
        if(category_enabled(kind,rules)) mask|=kind_bit(kind);
    }
    return mask;
}
inline bool selected(Kind kind,std::string_view name,std::string_view library,bool event,const Rules& rules) {
    if(!category_enabled(kind,rules)) return false;
    if(list_match("reflection,whitebox,placement_indicator,_proxy,_distant,australia_trees_0,australia_trees_tall_0,australia_barriers_0",name)) return false;
    if(list_match(rules.skip_models,name) || list_match(rules.exclude_libraries,library) || (event && rules.skip_events)) return false;
    if(kind==Kind::Tree) return prefix_match(rules.prefix,name);
    if(kind==Kind::Furniture) return is_solid_bench(name);
    if(list_match(rules.barrier_exclude,name)) return false;
    return is_barrier(kind);
}
struct Slowdown {
    float tree=0.12f,maximum=0.45f,exponent=1.5f,barrier=0.04f;
};
inline float speed_loss(Kind kind,float size,const Slowdown& options) {
    if(kind!=Kind::Tree && !is_barrier(kind)) return 0;
    float loss=kind==Kind::Tree?options.tree*std::pow(std::max(size,0.05f),options.exponent):options.barrier;
    if(!std::isfinite(loss) || !std::isfinite(options.maximum)) return 0;
    return std::clamp(loss,0.0f,std::clamp(options.maximum,0.0f,0.95f));
}
}
