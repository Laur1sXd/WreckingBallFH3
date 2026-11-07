#include "rules.h"
#include <cassert>
#include <iostream>
int main() {
    using wb::Kind;wb::Rules rules;
    auto choose=[&](const char* name,const char* lib="PROPS",bool event=false){return wb::selected(wb::classify(name,lib),name,lib,event,rules);};
    assert(wb::list_match(" Wood, WIRE ","FENCE_wire_3D"));assert(!wb::list_match("", "tree_pine"));
    assert(choose("TREE_Coast_Poinciana_01_3D","AUSTRALIA_TREES"));
    rules.trees=false;assert(!choose("TREE_Global_Gum_06_3D","AUSTRALIA_TREES"));rules.trees=true;
    rules.prefix="TREE_Global_Pine_,TREE_Global_Palm_";assert(!choose("TREE_Global_Gum_06_3D","AUSTRALIA_TREES"));assert(choose("TREE_Global_Pine_01","AUSTRALIA_TREES"));rules.prefix="*";
    rules.skip_models="Gum_06";assert(!choose("TREE_Global_Gum_06_3D","AUSTRALIA_TREES"));rules.skip_models="ROOT_";
    assert(!choose("BUSH_Coast_01","AUSTRALIA_BUSHES"));
    assert(!choose("TREE_Crops_Banana_02_3D","AUSTRALIA_TREES"));
    assert(!choose("Bush_Flatten_Soft_Sml_3D","AUSTRALIA_SMASHABLETEMPLATE"));
    assert(!choose("BARR_FENCE_CONCRETE","BARRIERS"));assert(!choose("BARR_FENCE_WOOD","BARRIERS",true));
    rules.skip_events=false;assert(choose("BARR_FENCE_WOOD","BARRIERS",true));
    rules.barriers=false;assert(!choose("BARR_FENCE_WOOD"));assert(!choose("PROP_Bollard_01"));rules.barriers=true;
    assert(!choose("SIGN_Speedlimit_01"));assert(!choose("PROP_Bench_01"));
    assert(choose("PROP_Surfers_Bench_02_3D"));
    assert(!choose("PROP_Surfers_Bench_03_3D"));
    rules.categories[size_t(Kind::Furniture)]=false;assert(!choose("PROP_Surfers_Bench_02_3D"));
    assert(wb::classify("BARR_Armco_01_4m","AUSTRALIA_BARRIERS")==Kind::Guardrail);
    assert(choose("BARR_Plains_Armco_Straight_01"));
    rules.categories[size_t(Kind::Guardrail)]=false;assert(!choose("BARR_Armco_01_4m"));assert(choose("BARR_FENCE_WOOD"));
    assert(wb::classify("PYLN_Electricity_Pole_01","PROPS")==Kind::Pole);
    assert(!choose("PYLN_Electricity_Pole_01","AUSTRALIA_TREES"));
    assert(!choose("prop_surfers_tram_pylon"));
    assert(!choose("PROP_Wheeliebin_01"));
    assert(!choose("area20_laketrees_reflection","AUSTRALIA_TREES"));
    assert(!choose("australia_trees_0","AUSTRALIA_TREES"));
    assert(!choose("PROP_Surfers_Tram_Pylon"));
    assert(choose("barr_meshfence_plastic_pole","BARRIERS"));
    assert(wb::classify("PROP_Streetlight_01","PROPS")==Kind::Streetlight);
    assert(wb::classify("BARR_Barrier_Wood_01","BARRIERS")==Kind::Fence);
    assert(wb::size_factor("Pine:1.3,Palm:0.9","TREE_Pine_01")==1.3f);
    assert(wb::size_factor("Pine:nan","TREE_Pine_01")==1);
    wb::Slowdown loss{0.12f,0.45f,1.5f,0.04f};
    assert(std::fabs(wb::speed_loss(Kind::Tree,1,loss)-0.12f)<0.0001f);
    assert(wb::speed_loss(Kind::Tree,10,loss)==0.45f);
    assert(wb::speed_loss(Kind::Tree,0.5f,loss)<0.12f);
    assert(wb::speed_loss(Kind::Other,1,loss)==0);
    assert(wb::speed_loss(Kind::Pole,1,loss)==0);
    loss.maximum=0;assert(wb::speed_loss(Kind::Tree,1,loss)==0);
    std::cout<<"Selection rules and slowdown calculation checks passed\n";
}
