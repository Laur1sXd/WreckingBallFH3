// Exercise actual port logic with synthetic providers, not a running game.
#include "../src/wreckingball_fh3.cpp"
#include <cassert>
#include <cstdlib>
#include <new>

namespace {
void mock_resize(GameVector* vector,size_t n) {
    vector->first=static_cast<byte*>(realloc(vector->first,n*sizeof(Group)));
    vector->last=vector->end=vector->first+n*sizeof(Group);
}
float* mock_decode(byte*,float* matrix,const byte* instance,float* scale) {
    matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
    matrix[12]=*reinterpret_cast<const float*>(instance);
    matrix[13]=*reinterpret_cast<const float*>(instance+4);
    matrix[14]=*reinterpret_cast<const float*>(instance+8);
    *scale=1;return matrix;
}
void jump_at(uintptr_t rva,void* target) {
    byte code[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    memcpy(code+2,&target,8);memcpy(g_base+rva,code,sizeof code);
}
void fracture_id_call(uintptr_t return_rva) {
    // The native detour substitutes an ID at exactly the two breakup call
    // sites. Reproduce their return addresses with a minimal x64 caller.
    byte code[]{0x48,0x83,0xEC,0x28,0xE8,0,0,0,0,0x48,0x83,0xC4,0x28,0xC3};
    const int32_t displacement=int32_t(OBJECT_DEFINITION_ID-return_rva);
    memcpy(code+5,&displacement,sizeof displacement);
    memcpy(g_base+return_rva-9,code,sizeof code);
}
struct MockData {
    alignas(16) byte data[0x100]{};
    alignas(16) byte batch[MODEL_BATCH_SIZE]{};
    alignas(16) byte instances[0x40]{};
    MockData(const char* lib,const char* name) {
        new(data) std::string(lib);new(data+0x20) std::string("");
        new(batch+0x48) std::string(name);
        *reinterpret_cast<GameVector*>(data+0xA0)={batch,batch+sizeof batch,batch+sizeof batch};
        *reinterpret_cast<GameVector*>(batch)={instances,instances+sizeof instances,instances+sizeof instances};
        *reinterpret_cast<float*>(instances)=10;*reinterpret_cast<float*>(instances+0x20)=20;
    }
    GameVector* groups(){return reinterpret_cast<GameVector*>(data+0x60);}
    ~MockData(){
        forget_points(data);
        take_deferred(data);
        reinterpret_cast<std::string*>(data)->~basic_string();
        reinterpret_cast<std::string*>(data+0x20)->~basic_string();
        reinterpret_cast<std::string*>(batch+0x48)->~basic_string();
        free(groups()->first);
    }
};
uint16_t observed_cell_count{};
byte* observed_message{};
void mock_add_cell(byte*,byte* message) {
    observed_message=message;observed_cell_count=*reinterpret_cast<uint16_t*>(message+0x68);
}
byte* mock_car{};
byte** mock_car_from_body(byte** result,byte* other) { *result=other?mock_car:nullptr;return result; }
int velocity_reads{},velocity_writes{};
float* mock_get_velocity(byte* dynamics,float* value) {
    ++velocity_reads;memcpy(value,dynamics-0x4A0,16);return value;
}
void mock_set_velocity(byte* dynamics,const float* value) {
    ++velocity_writes;memcpy(dynamics-0x4A0,value,16);
}
void mock_break(byte* object,void*) { *reinterpret_cast<int32_t*>(object+0x148)=2; }

void collision_safety() {
    alignas(16) byte provider[16]{};
    constexpr auto palm_alias=CLUTTER_FIRST+20;
    add_points(provider,{{30,0,0,1,1,0,true,wb::Kind::Tree},
                         {30,0,0,-0.9f,1,0,false,wb::Kind::Fence},
                         {32,0,0,1,1,0,true,wb::Kind::Tree,1,true,palm_alias}});
    alignas(16) byte data[0x120]{};
    auto& name=*new(data) std::string("Regrouped_3_square4693.procphys");
    auto* mesh=data+0xD8;
    const float box[]{26,-2,-4,0,34,4,4,0};
    const float side[]{29.8f,0,0,0, 30.2f,0,0,0, 30.2f,2,0,0};
    const float raised[]{29.8f,0.75f,-1,0, 30.2f,0.75f,1,0, 30.2f,0.75f,-1,0};
    const float offset[]{29.8f,0,3,0, 30.2f,0,3,0, 30.2f,2,3,0};
    const float ground[]{26,0,-4,0, 34,0,-4,0, 30,0,8,0};
    const float lower_ground[]{26,-3,-4,0, 34,-3,-4,0, 30,-3,8,0};
    const float cap[]{31.6f,1.1f,-0.3f,0, 32.4f,1.1f,0.3f,0, 32.4f,1.1f,-0.3f,0};
    const float raised_above_palm[]{31.6f,6.5f,-0.3f,0, 32.4f,6.5f,0.3f,0, 32.4f,6.5f,-0.3f,0};
    struct Face { byte flags, surface, pad[2]; uint32_t indices[3]; float normal[4]; };
    static_assert(sizeof(Face)==32);
    Face trunk{12,85,{}, {0,1,2},{}};
    Face wall{12,94,{}, {0,1,2},{}};
    Face fence{12,91,{}, {0,1,2},{}};
    Face floor{12,91,{}, {0,1,2},{}};
    Face distant{12,85,{}, {0,1,2},{}};
    Face terrain{12,14,{}, {0,1,2},{}};
    byte* faces[]{reinterpret_cast<byte*>(&trunk),reinterpret_cast<byte*>(&wall),
                  reinterpret_cast<byte*>(&fence),reinterpret_cast<byte*>(&floor),reinterpret_cast<byte*>(&distant)};
    const float* models[]{side,side,side,raised,offset};
    byte* out[5]{},*verts[5]{};
    o_query=[](byte*,const float*,int maximum,byte**,byte**,uint8_t,void*) { return maximum; };
    const auto run=[&](void* scratch=nullptr) {
        for (int i=0;i<5;++i) {
            out[i]=faces[i];verts[i]=reinterpret_cast<byte*>(const_cast<float*>(models[i]));
        }
        return query(mesh,box,5,out,verts,12,scratch);
    };
    name="bldg_coast_beachhotel_01_square0.phys";
    assert(run()==5 && out[0]==faces[0] && out[4]==faces[4]);
    name="unverified.procphys";
    assert(run()==5);
    name="Regrouped_3_square4693.procphys";
    assert(run(provider)==5);
    assert(run()==4 && out[0]==faces[1] && out[1]==faces[2] && out[2]==faces[3] && out[3]==faces[4]);
    name="autoterrain_x-7161_z6138_square24.phys";
    out[0]=reinterpret_cast<byte*>(&terrain);
    verts[0]=reinterpret_cast<byte*>(const_cast<float*>(lower_ground));
    assert(query(mesh,box,1,out,verts,12,nullptr)==1);
    name="Regrouped_5_square4693.procphys";
    assert(run()==4 && out[1]==faces[2]);
    name="autoterrain_x-7161_z6138_square24.phys";
    out[0]=reinterpret_cast<byte*>(&terrain);
    verts[0]=reinterpret_cast<byte*>(const_cast<float*>(ground));
    assert(query(mesh,box,1,out,verts,12,nullptr)==1 && out[0]==reinterpret_cast<byte*>(&terrain));
    name="Regrouped_5_square4693.procphys";
    assert(run()==3 && out[0]==faces[1] && out[1]==faces[3] && out[2]==faces[4]);
    assert(verts[1]==reinterpret_cast<const byte*>(raised));
    // A palm cap matches a verified tree surface and a tight trunk footprint.
    // The same geometry with a structural material stays solid.
    const auto cap_query=[&](byte surface,const float* geometry) {
        terrain.surface=surface;
        out[0]=reinterpret_cast<byte*>(&terrain);
        verts[0]=reinterpret_cast<byte*>(const_cast<float*>(geometry));
        return query(mesh,box,1,out,verts,12,nullptr);
    };
    assert(cap_query(85,cap)==1);
    alignas(16) byte palm[0x200]{};
    *reinterpret_cast<float*>(palm+0xE0)=32;
    *reinterpret_cast<float*>(palm+0xA0)=38;
    assert(!mark_prop_broken(palm,palm_alias+1,wb::Kind::Tree));
    assert(cap_query(85,cap)==1);
    assert(mark_prop_broken(palm,palm_alias,wb::Kind::Tree));
    assert(cap_query(85,cap)==0);
    assert(cap_query(85,raised_above_palm)==1);
    assert(cap_query(94,cap)==1);
    assert(cap_query(85,raised)==1); // other trees retain horizontal faces
    forget_points(provider);
    assert(run()==5);
    add_points(provider,{{32,0,0,1,1,0,true,wb::Kind::Tree,1,true,palm_alias}});
    assert(cap_query(85,cap)==1);
    forget_points(provider);
    const float bench_top[]{30.545f,.428f,-1.89f,0,30.008f,.428f,-1.89f,0,30.008f,.428f,1.89f,0};
    const float bench_side[]{30.545f,0,-1.89f,0,30.545f,.428f,-1.89f,0,30.545f,.428f,1.89f,0};
    const float kerb[]{30.545f,0,-1.89f,0,30.545f,.15f,-1.89f,0,30.545f,.15f,1.89f,0};
    constexpr auto bench_alias=CLUTTER_FIRST+21;
    add_points(provider,{{30,0,0,2,1,0,true,wb::Kind::Furniture,0,false,bench_alias,{0,0,1,0,1,0,-1,0,0}}});
    name="Regrouped_11_square4693.procphys";
    assert(cap_query(91,bench_top)==1 && cap_query(19,bench_side)==1);
    *reinterpret_cast<float*>(palm+0xE0)=30;
    assert(mark_prop_broken(palm,bench_alias,wb::Kind::Furniture));
    assert(cap_query(91,bench_top)==0 && cap_query(19,bench_side)==0);
    assert(cap_query(19,kerb)==1 && cap_query(19,bench_top)==1);
    for (const auto route:{69,70,136}) {
        name="Australia_procedural_routeset_"+std::to_string(route)+"_square4693.procphys";
        assert(cap_query(91,bench_top)==0 && cap_query(19,bench_side)==0 && cap_query(19,kerb)==1);
    }
    name="Australia_procedural_routeset_690_square4693.procphys";
    assert(cap_query(91,bench_top)==1 && cap_query(19,bench_side)==1);
    name="Regrouped_5_square4693.procphys";
    assert(cap_query(19,bench_side)==1 && cap_query(91,bench_top)==1);
    name="autoterrain_x-7161_z6138_square24.phys";
    assert(cap_query(91,bench_top)==1);
    forget_points(provider);
    name.~basic_string();
    puts("Stock structures, raised surfaces, unrelated scenery, and barriers above a drop retain collision");
}
}

int main() {
    g_base=static_cast<byte*>(VirtualAlloc(nullptr,IMAGE_SIZE,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    assert(g_base);
    jump_at(RESIZE_GROUPS,reinterpret_cast<void*>(mock_resize));
    jump_at(DECODE_INSTANCE,reinterpret_cast<void*>(mock_decode));
    g_points=static_cast<Point*>(VirtualAlloc(nullptr,sizeof(Point)*POINT_CAPACITY,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(g_points);for(auto& head:g_heads) head.store(-1);
    MockData trees("AUSTRALIA_TREES","TREE_Coast_Poinciana_01_3D");
    inject(trees.data);
    assert(trees.groups()->count(sizeof(Group))==1);
    auto tree_group=reinterpret_cast<Group*>(trees.groups()->first);
    const auto tree_alias=tree_group->definition;
    assert(tree_alias>=int(CLUTTER_FIRST) && tree_group->count==2);
    assert(g_point_count.load()==2 && g_deferred_data.count(trees.data)==1);
    inject(trees.data);
    assert(trees.groups()->count(sizeof(Group))==1 && g_point_count.load()==2);
    puts("Early tree injection and repeat-load deduplication passed");
    {
        MockData stock("AUSTRALIA_TREES","TREE_Global_Gum_06_3D");
        mock_resize(stock.groups(),1);
        const Group original{0x16A,0,0,2,-1,0,0};
        memcpy(stock.groups()->first,&original,sizeof original);
        inject(stock.data);
        assert(stock.groups()->count(sizeof(Group))==1);
        assert(memcmp(stock.groups()->first,&original,sizeof original)==0);
        assert(!g_provider_points.count(stock.data) && g_active_points.load()==2);
    }
    puts("Stock breakable trees retain their exact native group and collision coverage");
    {
        MockData streamed("AUSTRALIA_TREES","TREE_Coast_Poinciana_01_3D");
        g_rules.trees=false;publish_scenery();
        inject(streamed.data);
        assert(streamed.groups()->count(sizeof(Group))==0 && g_active_points.load()==2);
        g_rules.trees=true;publish_scenery();
        inject(streamed.data);
        assert(streamed.groups()->count(sizeof(Group))==1 && g_point_count.load()==2 && g_active_points.load()==2);
        o_procedural_destructor=[](byte*) {};
        procedural_destructor(trees.data);
        assert(g_active_points.load()==2 && !g_deferred_data.count(trees.data));
        procedural_destructor(streamed.data);
        assert(g_active_points.load()==0 && g_point_count.load()==2);
        add_points(trees.data,{{10,0,0,1,1,0,true,wb::Kind::Tree},{20,0,0,1,1,0,true,wb::Kind::Tree}});
        inject(trees.data);
    }
    puts("Live settings and overlapping streamed collision-point lifetimes passed");
    MockData early_armco("AUSTRALIA_BARRIERS","BARR_Armco_01_4m_3D");inject(early_armco.data);
    MockData early_pole("PROPS","PYLN_Electricity_Pole_01");inject(early_pole.data);
    assert(early_pole.groups()->count(sizeof(Group))==0);
    assert(early_armco.groups()->count(sizeof(Group))==1);
    uint32_t armco_alias=reinterpret_cast<Group*>(early_armco.groups()->first)->definition;
    assert(armco_alias>=CLUTTER_FIRST && armco_alias<CLUTTER_END);
    MockData railing("AUSTRALIA_BARRIERS","BARR_Metal_Railing_02_2m_3D");inject(railing.data);
    assert(railing.groups()->count(sizeof(Group))==1);
    wb::Kind railing_kind{};float railing_size{};
    assert(custom_kind(reinterpret_cast<Group*>(railing.groups()->first)->definition,&railing_kind,&railing_size) &&
           railing_kind==wb::Kind::Railing);
    puts("Audited Armco uses its named donor before the native table is populated");

    alignas(16) byte native_definition[DEFINITION_SIZE]{};
    alignas(16) byte donor_pieces[2][DEFINITION_SIZE]{};
    byte* native_pieces[]{donor_pieces[0],donor_pieces[1]};
    byte hulls[2]{};
    for (int i=0;i<2;++i) {
        *reinterpret_cast<uint32_t*>(native_pieces[i]+0x74)=1;
        *reinterpret_cast<byte**>(native_pieces[i]+0x80)=hulls+i;
        *reinterpret_cast<float*>(native_pieces[i]+0x68)=3.0f+i;
        *reinterpret_cast<int*>(native_pieces[i]+0xC0)=i;
    }
    *reinterpret_cast<byte***>(native_definition+0x88)=native_pieces;
    *reinterpret_cast<float*>(native_definition+DEFINITION_MASS)=200.0f;
    *reinterpret_cast<uint32_t*>(native_definition+0x94)=2;
    alignas(16) byte table[32]{};byte* roots[]={native_definition};
    *reinterpret_cast<uintptr_t*>(table)=reinterpret_cast<uintptr_t>(g_base)+TABLE_VTABLE;
    *reinterpret_cast<byte***>(table+8)=roots;
    *reinterpret_cast<uint64_t*>(table+0x10)=1;
    Definitions definitions_state{};
    definitions_state.table=table;definitions_state.list=reinterpret_cast<byte*>(roots);
    definitions_state.first=native_definition;definitions_state.end_token=reinterpret_cast<byte*>(1);
    definitions_state.header_token=*reinterpret_cast<uintptr_t*>(table);
    definitions_state.items.push_back({5,native_definition,"prop_bench_01"});
    definitions_state.items.push_back({6,native_definition,"pyln_electricity_pole_01"});
    definitions_state.items.push_back({7,native_definition,"barr_fence_metalmesh_02_4m"});
    definitions_state.items.push_back({8,native_definition,"barr_fence_metalmesh_01_post"});
    definitions_state.items.push_back({9,native_definition,"tree_gorge_ghostgum_04_smashable"});
    definitions_state.items.push_back({10,native_definition,"barr_plains_bollard_01"});
    alignas(16) byte native_palm[DEFINITION_SIZE]{},native_palm_shape[0x30]{};
    memcpy(native_palm,native_definition,DEFINITION_SIZE);
    native_palm_shape[0]=5;
    native_palm_shape[1]=0x0F;
    byte* palm_shapes[]{native_palm_shape};
    *reinterpret_cast<byte***>(native_palm+0x80)=palm_shapes;
    *reinterpret_cast<uint32_t*>(native_palm+0x90)=1;
    *reinterpret_cast<float*>(native_palm+0xD4)=15;
    *reinterpret_cast<GameVector*>(native_palm+0xA0)={native_palm_shape,native_palm_shape+0x30,native_palm_shape+0x30};
    definitions_state.items.push_back({11,native_palm,"tree_global_palm_02"});
    definitions_state.items.push_back({12,native_palm,"prop_surfers_bench_03"});
    definitions_state.australia=true;
    definitions_state.utility_poles.set(6);
    g_definitions.push_back(&definitions_state);
    g_active_definitions.store(&definitions_state);
    assert(select_definition(&definitions_state,armco_alias)->id==7);
    assert(model(table,0xFFFF0000|armco_alias)==42533);
    assert(model(table,armco_alias)==42533);
    assert(model(table,0xFFFF0000|tree_alias)==47186);
    assert(*reinterpret_cast<uint32_t*>(lookup(table,0xFFFF0000|tree_alias)+0x94)==0);
    assert(*reinterpret_cast<uint32_t*>(native_definition+0x94)==2);
    MockData palm("AUSTRALIA_TREES","TREE_Global_Palm_01_3D");
    inject(palm.data);
    assert(palm.groups()->count(sizeof(Group))==1);
    const auto palm_alias=reinterpret_cast<Group*>(palm.groups()->first)->definition;
    assert(select_definition(&definitions_state,palm_alias)->id==11);
    assert(model(table,0xFFFF0000|palm_alias)==47207);
    auto palm_root=lookup(table,0xFFFF0000|palm_alias);
    auto shape=**reinterpret_cast<byte***>(palm_root+0x80);
    auto spheres=*reinterpret_cast<const float(**)[4]>(shape+8);
    assert(shape!=native_palm_shape && shape[0]==5 && shape[1]==0x0F);
    assert(*reinterpret_cast<uint32_t*>(shape+0x10)==2 && spheres[1][3]>1.1f);
    assert(*reinterpret_cast<float*>(palm_root+0xD4)==3 && *reinterpret_cast<float*>(native_palm+0xD4)==15);
    assert(*reinterpret_cast<uint32_t*>(palm_root+0x94)==0);
    MockData armco("AUSTRALIA_BARRIERS","BARR_Armco_01_4m_3D");
    mock_resize(armco.groups(),1);
    const Group native_span{7,0,0,1,-1,0,0};
    *reinterpret_cast<Group*>(armco.groups()->first)=native_span;
    inject(armco.data);
    assert(armco.groups()->count(sizeof(Group))==2);
    auto groups=reinterpret_cast<Group*>(armco.groups()->first);
    assert(memcmp(groups,&native_span,sizeof native_span)==0);
    assert(groups[1].definition==int(armco_alias) && groups[1].start==1 && groups[1].count==1);
    MockData bollard("PROPS","BARR_Plains_Bollard_01_VAR_A_3D");
    inject(bollard.data);
    assert(bollard.groups()->count(sizeof(Group))==1);
    assert(select_definition(&definitions_state,reinterpret_cast<Group*>(bollard.groups()->first)->definition)->id==10);
    MockData bench("PROPS","PROP_Surfers_Bench_02_3D");
    inject(bench.data);
    assert(bench.groups()->count(sizeof(Group))==1);
    const auto bench_alias=reinterpret_cast<Group*>(bench.groups()->first)->definition;
    auto bench_root=lookup(table,0xFFFF0000|bench_alias);
    auto bench_debris=lookup(table,bench_alias);
    assert(bench_root && bench_debris && model(table,bench_alias)==45347);
    assert(*reinterpret_cast<uint32_t*>(bench_root+0x94)==1 && *reinterpret_cast<uint32_t*>(bench_debris+0x74)==1);
    auto bench_shape=**reinterpret_cast<byte***>(bench_debris+0x80);
    const auto bench_corners=*reinterpret_cast<const float(**)[4]>(bench_shape+8);
    assert(bench_shape[0]==5 && *reinterpret_cast<uint32_t*>(bench_shape+0x10)==8);
    assert(bench_corners[0][0]<-1.9f && bench_corners[7][1]>0.43f && bench_corners[0][2]<-0.55f);
    assert(memcmp(bench_debris+0xA0,native_palm+0xA0,sizeof(GameVector))==0);
    assert(*reinterpret_cast<const float*>(bench_debris+DEFINITION_MASS)==200.0f);
    const auto inertia=reinterpret_cast<const float*>(bench_debris+8);
    const auto inverse=reinterpret_cast<const float*>(bench_debris+0x2C);
    for (int i=0;i<9;++i) {
        if (i%4==0) {
            assert(std::isfinite(inertia[i]) && inertia[i]>1.0f);
            assert(std::isfinite(inverse[i]) && inverse[i]>0.0f);
            assert(std::fabs(inertia[i]*inverse[i]-1.0f)<0.0001f);
        } else assert(inertia[i]==0.0f && inverse[i]==0.0f);
    }
    MockData unsupported("PROPS","BARR_Bollard_Concrete");
    inject(unsupported.data);
    assert(unsupported.groups()->count(sizeof(Group))==0);
    MockData unknown("PROPS","PROP_Unmatched_01_3D");inject(unknown.data);
    assert(unknown.groups()->count(sizeof(Group))==0);
    MockData protected_object("BUILDINGS","BARR_Armco_01_4m_3D");inject(protected_object.data);
    assert(protected_object.groups()->count(sizeof(Group))==0);
    puts("Original tree, palm and Armco visuals, mixed groups and protected models passed");

    alignas(16) byte manager[0x600]{};alignas(16) byte provider[0x1D0]{};
    *reinterpret_cast<byte**>(manager+0x4D8)=table;
    *reinterpret_cast<uintptr_t*>(provider)=reinterpret_cast<uintptr_t>(g_base)+TREE_PROVIDER_VTABLE;
    *reinterpret_cast<byte**>(provider+0x1C8)=trees.data;
    alignas(16) byte message[0x70]{};
    *reinterpret_cast<byte**>(message+0x60)=provider;
    *reinterpret_cast<uint16_t*>(message+0x68)=0;
    o_add_cell=mock_add_cell;add_cell(manager,message);
    assert(observed_cell_count==2 && observed_message!=message);
    assert(*reinterpret_cast<uint16_t*>(message+0x68)==0);
    puts("Deferred cell count synchronization without sender mutation passed");

    alignas(16) byte loose[0x200]{},intact[0x200]{},placed[0x200]{};
    {
        alignas(16) byte presentation[0x40]{},track[0xFBC0]{},models[2][16]{};
        byte* rows[]{models[0],models[1]};
        for (int i=0;i<2;++i) *reinterpret_cast<byte**>(models[i]+8)=hulls+i;
        *reinterpret_cast<uintptr_t*>(presentation)=reinterpret_cast<uintptr_t>(g_base)+0x4684FA8;
        *reinterpret_cast<byte**>(presentation+0x38)=track;
        *reinterpret_cast<byte**>(manager+0x10)=presentation;
        *reinterpret_cast<GameVector*>(track+0xF6F8)={reinterpret_cast<byte*>(rows),reinterpret_cast<byte*>(rows+2),reinterpret_cast<byte*>(rows+2)};
        const auto& resident=*reinterpret_cast<const GameVector*>(track+0xF6F8);
        const auto* ghostgum=exact_definition(&definitions_state,"tree_gorge_ghostgum_04_smashable");
        const auto* palm_match=exact_definition(&definitions_state,"tree_global_palm_02");
        assert(fracture_donor(definitions_state,resident,*ghostgum,"tree_global_palm_01",wb::Kind::Tree)==palm_match);
        assert(!fracture_donor(definitions_state,resident,*ghostgum,"tree_global_pine_02",wb::Kind::Tree));
        assert(fracture_donor(definitions_state,resident,*palm_match,"tree_gorge_ghostgum_03",wb::Kind::Tree)==ghostgum);
        *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFF0000|palm_alias;
        *reinterpret_cast<float*>(intact+0xE0)=10;
        const auto original_id=*reinterpret_cast<uint32_t*>(intact+0x128);
        jump_at(OBJECT_DEFINITION_ID,reinterpret_cast<void*>(object_definition_id));
        fracture_id_call(FRACTURE_ROOT_RETURN);
        fracture_id_call(FRACTURE_CHILD_RETURN);
        o_object_definition_id=[](byte* object,uint32_t* out) {
            *out=*reinterpret_cast<const uint32_t*>(object+0x128);return out;
        };
        o_bind_loose_model=[](byte* mgr,int slot,byte* object,int visual,const float*) {
            auto table=*reinterpret_cast<byte**>(mgr+0x4D8);
            const auto id=*reinterpret_cast<const uint32_t*>(object+0x128);
            auto render=lookup(table,id);
            assert(slot==7 && visual==1 && *reinterpret_cast<int*>(render+0xC0)==visual);
            assert(*reinterpret_cast<uint32_t*>(render+0x74)==0);
            assert(*reinterpret_cast<float*>(render+0x68)==4.0f);
        };
        o_break_pieces=[](byte* mgr,byte* object) {
            uint32_t original{},fracture{},child_parent{};
            object_definition_id(object,&original);
            reinterpret_cast<ObjectDefinitionIdFn>(g_base+FRACTURE_ROOT_RETURN-9)(object,&fracture);
            reinterpret_cast<ObjectDefinitionIdFn>(g_base+FRACTURE_CHILD_RETURN-9)(object,&child_parent);
            assert(original!=fracture && fracture==child_parent && fracture>>16==0xFFFF);
            auto table=*reinterpret_cast<byte**>(mgr+0x4D8);
            auto root=lookup(table,fracture);
            assert(*reinterpret_cast<unsigned*>(root+0x94)==2);
            assert(*reinterpret_cast<float*>(root+0x68)==0.0f);
            const auto first=fracture&0xFFFF,second=first|0x10000;
            auto a=lookup(table,first),b=lookup(table,second);
            assert(model(table,first)==0 && model(table,second)==1);
            assert(*reinterpret_cast<byte**>(b+0x80)==*reinterpret_cast<byte**>(a+0x80)+1);
            alignas(16) byte fragment[0x200]{};
            *reinterpret_cast<uint32_t*>(fragment+0x128)=second;
            *reinterpret_cast<int32_t*>(fragment+0x148)=5;
            bind_loose_model(mgr,7,fragment,1,nullptr);
            assert(*reinterpret_cast<uint32_t*>(lookup(table,second)+0x74)==1);
        };
        break_pieces(manager,intact);
        assert(*reinterpret_cast<uint32_t*>(intact+0x128)==original_id && !g_fracture_source.object && !g_loose_visual.definition);
        // A visually different standing-body donor must never replace a
        // converted Armco span's intact model with the donor's loose parts.
        const auto saved_break=o_break_pieces;
        o_break_pieces=[](byte* mgr,byte*) { mgr[0x562]=!g_fracture_source.object; };
        *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFF0000|armco_alias;
        break_pieces(manager,intact);
        assert(manager[0x562]);
        *reinterpret_cast<uint32_t*>(intact+0x128)=original_id;
        o_break_pieces=saved_break;
        jump_at(RECORD_BROKEN_KEY,reinterpret_cast<void*>(+[](byte* marks,const byte*) { *marks=1; }));
        jump_at(RELEASE_OBJECT,reinterpret_cast<void*>(+[](byte* mgr,byte*,unsigned reason) {
            assert(reason==0 && mgr[0x560]==1);mgr[0x561]=1;
        }));
        *reinterpret_cast<byte**>(models[1]+8)=nullptr;
        break_pieces(manager,intact);
        assert(manager[0x561]==1);
        std::vector<byte*> bench_models(BENCH_MODELS[2]+1);
        auto bench_rows=reinterpret_cast<byte*>(bench_models.data());
        auto bench_rows_end=reinterpret_cast<byte*>(bench_models.data()+bench_models.size());
        *reinterpret_cast<GameVector*>(track+0xF6F8)={bench_rows,bench_rows_end,bench_rows_end};
        *reinterpret_cast<byte**>(models[1]+8)=hulls+1;
        bench_models[BENCH_MODELS[2]]=models[1];
        assert(resident_bench_model(manager)==BENCH_MODELS[2]);
        bench_models[BENCH_MODELS[1]]=models[0];
        assert(resident_bench_model(manager)==BENCH_MODELS[1]);
        bench_models[BENCH_MODELS[0]]=models[0];
        assert(resident_bench_model(manager)==BENCH_MODELS[0]);
        bench_models[BENCH_MODELS[0]]=nullptr;
        *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFF0000|bench_alias;
        o_break_pieces=[](byte* mgr,byte* object) {
            alignas(16) byte fragment[0x200]{};
            const auto alias=*reinterpret_cast<uint32_t*>(object+0x128)&0xFFFF;
            *reinterpret_cast<uint32_t*>(fragment+0x128)=alias;
            *reinterpret_cast<int32_t*>(fragment+0x148)=5;
            auto table=*reinterpret_cast<byte**>(mgr+0x4D8);
            assert(model(table,alias)==BENCH_MODELS[0]);
            bind_loose_model(mgr,8,fragment,model(table,alias),nullptr);
            assert(model(table,alias)==BENCH_MODELS[0] && !g_loose_visual.definition);
        };
        o_bind_loose_model=[](byte* mgr,int slot,byte* object,int visual,const float*) {
            auto table=*reinterpret_cast<byte**>(mgr+0x4D8);
            auto render=lookup(table,*reinterpret_cast<uint32_t*>(object+0x128));
            assert(slot==8 && visual==BENCH_MODELS[1] && *reinterpret_cast<int*>(render+0xC0)==visual);
            assert(*reinterpret_cast<uint32_t*>(render+0x74)==0);
            mgr[0x563]=1;
        };
        manager[0x561]=0;
        break_pieces(manager,intact);
        assert(manager[0x563]==1 && manager[0x561]==0);
        bench_models[BENCH_MODELS[1]]=nullptr;
        bench_models[BENCH_MODELS[2]]=nullptr;
        assert(resident_bench_model(manager)<0);
        manager[0x563]=0;
        break_pieces(manager,intact);
        assert(manager[0x563]==0 && manager[0x561]==1);
        *reinterpret_cast<byte**>(manager+0x10)=nullptr;
        *reinterpret_cast<uint32_t*>(intact+0x128)=0;
    }
    puts("Tree break resolves separate stock models and hulls, preserves the original ID and skips invisible debris");
    puts("Concrete bench debris binds the best resident model of the same bench");
    byte* slots[]={loose,intact,placed};
    *reinterpret_cast<GameVector*>(manager+0x2A0)={reinterpret_cast<byte*>(slots),reinterpret_cast<byte*>(slots+3),reinterpret_cast<byte*>(slots+3)};
    *reinterpret_cast<int32_t*>(g_base+0x42EA068)=23;
    *reinterpret_cast<int32_t*>(loose+0x148)=5;
    *reinterpret_cast<int32_t*>(loose+0xC8)=101;
    *reinterpret_cast<int32_t*>(placed+0x148)=5;
    *reinterpret_cast<int32_t*>(placed+0xC8)=-1;
    *reinterpret_cast<int32_t*>(placed+0xCC)=23;
    *reinterpret_cast<int32_t*>(placed+0xD0)=-1;
    *reinterpret_cast<float*>(loose+0x120)=3.25f;
    update_debris(manager,100);update_debris(manager,144);
    assert(*reinterpret_cast<uint16_t*>(loose+0x124)==0);
    update_debris(manager,146);
    assert(*reinterpret_cast<uint16_t*>(loose+0x124)==0x4000);
    assert(*reinterpret_cast<float*>(loose+0x120)==3.25f);
    assert(*reinterpret_cast<uint16_t*>(intact+0x124)==0 && *reinterpret_cast<uint16_t*>(placed+0x124)==0);
    *reinterpret_cast<uint16_t*>(loose+0x124)=0;
    *reinterpret_cast<int32_t*>(loose+0xC8)=102;
    update_debris(manager,200);update_debris(manager,201);
    assert(*reinterpret_cast<uint16_t*>(loose+0x124)==0);
    *reinterpret_cast<float*>(loose+0xA0)=121;
    update_debris(manager,202);
    assert(*reinterpret_cast<uint16_t*>(loose+0x124)==0x4000);
    *reinterpret_cast<uint32_t*>(loose+0x128)=0xFFFF0006;
    *reinterpret_cast<uint16_t*>(loose+0x124)=0;
    update_debris(manager,203);update_debris(manager,250);
    assert(*reinterpret_cast<uint16_t*>(loose+0x124)==0);
    puts("Debris lifetime, distance, slot reuse, placed-object exclusion and FH3 fade offset passed");
    *reinterpret_cast<uint32_t*>(loose+0x128)=0xFFFF0005;
    g_debris_cleanup=false;*reinterpret_cast<uint16_t*>(loose+0x124)=0;
    update_debris(manager,300);assert(*reinterpret_cast<uint16_t*>(loose+0x124)==0);
    puts("All runtime regression checks passed");

    jump_at(CAR_FROM_BODY,reinterpret_cast<void*>(mock_car_from_body));
    jump_at(GET_VELOCITY,reinterpret_cast<void*>(mock_get_velocity));
    jump_at(SET_VELOCITY,reinterpret_cast<void*>(mock_set_velocity));
    alignas(16) byte car[0x7200]{},dynamics_storage[0x2000]{};
    auto dynamics=dynamics_storage+0x4B0;
    mock_car=car;
    *reinterpret_cast<byte**>(car+0x71E8)=dynamics;
    *reinterpret_cast<uintptr_t*>(dynamics)=reinterpret_cast<uintptr_t>(g_base)+DYNAMICS_VTABLE;
    for (auto vtable:{DYNAMICS_VTABLE,TRACKED_DYNAMICS_VTABLE}) {
        *reinterpret_cast<byte**>(g_base+vtable+0x328)=g_base+GET_VELOCITY;
        *reinterpret_cast<byte**>(g_base+vtable+0x330)=g_base+SET_VELOCITY;
    }
    alignas(16) byte contact[16]{};*reinterpret_cast<byte**>(contact+8)=car;
    auto velocity=reinterpret_cast<float*>(dynamics_storage+0x10);velocity[0]=100;velocity[3]=7;
    g_slowdown={0.12f,0.45f,1.5f,0.04f};
    o_collision=mock_break;
    *reinterpret_cast<int32_t*>(intact+0x148)=0;
    *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFFF004;
    g_rules.trees=false;publish_scenery();
    collision(intact,contact);
    assert(*reinterpret_cast<int32_t*>(intact+0x148)==0 && velocity_writes==0);
    g_rules.trees=true;publish_scenery();
    collision(intact,contact);
    assert(std::fabs(velocity[0]-88)<0.0001f && velocity[3]==7);
    assert(velocity_writes==1 && velocity_reads==1);
    collision(intact,contact);assert(velocity_writes==1);
    *reinterpret_cast<int32_t*>(intact+0x148)=0;
    *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFF0005;
    g_rules.enabled=false;publish_scenery();
    collision(intact,contact);
    assert(*reinterpret_cast<int32_t*>(intact+0x148)==2 && velocity_writes==1);
    g_rules.enabled=true;publish_scenery();
    // A native FH3 bench still breaks with its original impact response.
    *reinterpret_cast<byte**>(intact+0x40)=manager;
    *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFF0005;
    *reinterpret_cast<int32_t*>(intact+0x148)=0;
    velocity[0]=100;collision(intact,contact);assert(velocity[0]==100 && velocity_writes==1);
    // The deletion-tracked player-car layout was rejected by the old code.
    *reinterpret_cast<uintptr_t*>(dynamics)=reinterpret_cast<uintptr_t>(g_base)+TRACKED_DYNAMICS_VTABLE;
    *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFFF004;
    *reinterpret_cast<int32_t*>(intact+0x148)=0;
    g_slowdown.tree=0.20f;
    collision(intact,contact);assert(std::fabs(velocity[0]-80)<0.0001f && velocity_writes==2);
    add_points(trees.data,{{4,2,4,2,1,0,true,wb::Kind::Tree,2}});
    float pos[]{4,2,4};
    memcpy(intact+0xA0,pos,sizeof pos);
    *reinterpret_cast<int32_t*>(intact+0x148)=0;
    velocity[0]=100;
    collision(intact,contact);assert(std::fabs(velocity[0]-55)<0.0001f && velocity_writes==3);
    velocity[0]=100;apply_speed_loss(contact,wb::Kind::Tree,10,g_slowdown);assert(std::fabs(velocity[0]-55)<0.0001f);
    *reinterpret_cast<uint32_t*>(intact+0x128)=0xFFFF0006;
    *reinterpret_cast<int32_t*>(intact+0x148)=0;
    velocity[0]=100;collision(intact,contact);
    assert(*reinterpret_cast<int32_t*>(intact+0x148)==2 && velocity[0]==100);
    auto written=velocity_writes;g_slowdown.maximum=0;
    assert(!apply_speed_loss(contact,wb::Kind::Tree,1,g_slowdown) && velocity_writes==written);
    g_slowdown.maximum=0.45f;*reinterpret_cast<uintptr_t*>(dynamics)=0;
    assert(!apply_speed_loss(contact,wb::Kind::Tree,1,g_slowdown) && velocity_writes==written);
    puts("FH3 slowdown routing, scaling, cap, utility-pole exclusion, one loss per break, zero loss and dynamics identity checks passed");

    wchar_t current[MAX_PATH];GetCurrentDirectoryW(MAX_PATH,current);
    std::wstring settings_path=std::wstring(current)+L"\\runtime-settings-test.ini";
    WritePrivateProfileStringW(L"WreckingBall",L"SpeedLoss",L"0.125",settings_path.c_str());
    WritePrivateProfileStringW(L"WreckingBall",L"SpeedLossMax",L"0.5",settings_path.c_str());
    WritePrivateProfileStringW(L"WreckingBall",L"SpeedLossExponent",L"nan",settings_path.c_str());
    WritePrivateProfileStringW(L"WreckingBall",L"Guardrails",L"0",settings_path.c_str());
    WritePrivateProfileStringW(L"WreckingBall",L"BarrierNeedsGround",L"0",settings_path.c_str());
    load_settings(settings_path);
    assert(g_slowdown.tree==0.125f && g_slowdown.maximum==0.5f && g_slowdown.exponent==1.5f);
    assert(!g_rules.categories[size_t(wb::Kind::Guardrail)] && !g_ground_check);
    WritePrivateProfileStringW(L"WreckingBall",L"BarrierNeedGround",L"1",settings_path.c_str());
    load_settings(settings_path);assert(g_ground_check);
    DeleteFileW(settings_path.c_str());
    puts("Decimal INI parsing, invalid values, category switches and legacy ground-key migration passed");
    collision_safety();
}
