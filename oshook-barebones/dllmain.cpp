// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "osrs.h"
uint64_t doaction_tramp;
uint64_t renderscene_tramp = NULL;
uint64_t projectile_tramp = NULL;
PLH::CapstoneDisassembler* disasm = nullptr;
PLH::x64Detour* renderscene_hook = nullptr;
PLH::x64Detour* projectile_hook = nullptr;
PLH::x64Detour* doaction_hook = nullptr;
namespace osrs {
    namespace functions {
        fn_world_to_screen worldtoscreen = nullptr;
        fn_get_object_definition get_object_definition = nullptr;
        fn_fix_object_definition fix_object_definition = nullptr;
        fn_fix_npc_definition fix_npc_definition = nullptr;
        fn_get_item_definition get_item_definition = nullptr;
        fn_render_scene render_scene = nullptr;
        fn_do_action doaction = nullptr;
        fn_print_chat print_chat = nullptr;
        fn_projectiles projectiles = nullptr;
        fn_get_varb_definition get_varb_definition = nullptr;
    	void init_offsets(uintptr_t base)
    	{
            worldtoscreen = (fn_world_to_screen)(base + 0x7d4c0);
            get_object_definition = (fn_get_object_definition)(base + 0xfc0a0);
            fix_object_definition = (fn_fix_object_definition)(base + 0xfe540);
            fix_npc_definition = (fn_fix_npc_definition)(base + 0xdcce0);
            get_item_definition = (fn_get_item_definition)(base + 0xdd240);
            render_scene = (fn_render_scene)(base + 0x617f0);
            doaction = (fn_do_action)(base + 0x6c8e0);
            print_chat = (fn_print_chat)(base + 0x54c80);
            projectiles = (fn_projectiles)(base + 0x95110);
            get_varb_definition = (fn_get_varb_definition)(base + 0xe3190);
    	}
    }
	
	namespace offsets
    {
        uintptr_t local_player = 0;

        uintptr_t player_count = 0;
        uintptr_t player_indices = 0;
        uintptr_t player_list = 0;

        uintptr_t npc_count = 0;
        uintptr_t npc_indices = 0;
        uintptr_t npc_list = 0;

        uintptr_t scene = 0;
        uintptr_t ground_item_list = 0;
        uintptr_t interface_list = 0;

        uintptr_t interaction_timestamp = 0;
        uintptr_t timer = 0;
        uintptr_t tick_timer = 0;

        uintptr_t mouseover = 0;

        uintptr_t varp_array = 0;
        uintptr_t varbit_mask = 0;

        uintptr_t login_state = 0;
        uintptr_t game_state = 0;

        uintptr_t w2s_result_x = 0;
        uintptr_t w2s_result_y = 0;

        uintptr_t tileflags = 0;

        uintptr_t res_x = 0;
        uintptr_t res_y = 0;

        uintptr_t item_containers = 0;
    	
        uintptr_t window_hook = 0;
    	void init_offsets(uintptr_t base)
    	{
            player_list = base+0x1b367a0;
            player_indices = base+0x5bfbc0;
            player_count = base+0x5b7bb8;
            local_player = base+0x1b3e7a8;
            npc_list = base+0x1b41a30;
            npc_indices = base+0x1bc1a40;
            npc_count = base+0x5c35a8;
            interface_list = base+0x2037d88;
            scene = base+0x1bc1a48;
            ground_item_list = base+0x1bc1a50;
            mouseover = base+0x5ced20;
            varbit_mask = base+0x386fc0;
            varp_array = base+0x2037d60;
            login_state = base+0x5cdf74;
            game_state = base+0x493d88;
            w2s_result_x = base+0x493d08;
            w2s_result_y = base+0x493d0c;
            tileflags = base+0x2041220;
            res_x = base+0x5c34a8;
            res_y = base+0x5c34a4;
            item_containers = base+0x1784390;
            window_hook = base+0x177d2e8;
    	}
    }
}
// id is the chat tab to print to, 99 prints to game tab & console.
void print_chat(int id, const std::string& name, const std::string& msg) {
	//eastl strings, you can find a compatible eastl version or make a class for this.
	const char* p1[4] = { name.c_str(), name.c_str() + name.size(), name.c_str() + name.capacity(), nullptr }; // this is the name of the chatter, if not printing to game chat
    const char* p2[4] = { msg.c_str(), msg.c_str() + msg.size(), msg.c_str() + msg.capacity(), nullptr }; // the actual message
    osrs::functions::print_chat(id, p1, p2);
}

void hook_do_action(int pos_1, uint32_t pos_2, int action_id, int target_id, uintptr_t param_5, const char** param_6,
    uintptr_t param_7, uint32_t param_8, uintptr_t interface_ptr) {
	//wouldn't want to tamper with these.. might enable botting or some shit like that and it's illegal
    std::string msg = std::format("<col=ff0000>action: pos: ({}, {}), action id: {}, target id: {}</col>", pos_1, pos_2,
        action_id, target_id);

    print_chat(99, "name", msg);

    PLH::FnCast(doaction_tramp, osrs::functions::fn_do_action())(pos_1, pos_2, action_id, target_id, param_5, param_6,
        param_7, param_8, interface_ptr);
}

bool w2s(int floor, const osrs::vec3& in, osrs::vec2& out) {
    osrs::functions::worldtoscreen(nullptr, floor, in.x, in.y, in.z);

    out.y = *reinterpret_cast<int*>(osrs::offsets::w2s_result_y);
    out.x = *reinterpret_cast<int*>(osrs::offsets::w2s_result_x);
    if (out.x == -1 || out.y == -1) {
        return false;
    }
    return true;
}

void hook_projectiles() {
	//this is hooked as everything should be added to renderlist
	//we iterate scene and add highlight flags to stuff for lols, reversing what the highlight integer does is a task for the paster.
	osrs::player* local_player = *reinterpret_cast<osrs::player**>(osrs::offsets::local_player);
    auto scene_data = *reinterpret_cast<osrs::scene**>(osrs::offsets::scene);
    if (scene_data && scene_data->tile_array && local_player)
        for (int f = 0; f < 4; f++) {
            for (int x = std::clamp((local_player->position_x >> 7) - 25, 0, scene_data->scene_x); x < std::clamp(
                (local_player->position_x >> 7) + 25, 0, scene_data->scene_x); x++) {
                for (int y = std::clamp((local_player->position_y >> 7) - 25, 0, scene_data->scene_y); y < std::clamp(
                    (local_player->position_y >> 7) + 25, 0, scene_data->scene_y); y++) {
                    osrs::scene_tile* tile = scene_data->tile_array[f * scene_data->scene_x * scene_data->scene_y + x * scene_data->scene_x + y].tile;
                    if (!tile)
                        continue;
                    if (tile->attached) {
                        if (!tile->attached->object_highlight)
                            tile->attached->object_highlight = 0x5001E000157BF; 
                    	
                    }
                    for (int i = 0; i < tile->renderable_count; i++) {
                        osrs::renderable_object* object = tile->renderables[i].renderable;
                        if (!object)
                            continue;
                        if (!object->object_highlight)
                            object->object_highlight = 0x5001E000157BF;
                    }
                }
            }
        }
    PLH::FnCast(projectile_tramp, osrs::functions::fn_projectiles())();
}
int read_varb(int id) {
    osrs::osrs_ptr test;
    osrs::osrs_ptr* res = static_cast<osrs::osrs_ptr*>(osrs::functions::get_varb_definition(&test, id));
    if (res && res->ptr_object) {
        auto varbdef = static_cast<osrs::varb_definition*>(res->ptr_object);
        int* varbmasks = reinterpret_cast<int*>(osrs::offsets::varbit_mask);
        int* varps = *reinterpret_cast<int**>(osrs::offsets::varp_array);
        int mask = varbmasks[varbdef->bit2 - varbdef->bit1];
        int varp = varps[varbdef->id];
        return (mask & varp >> ((uint8_t)varbdef->bit1 & 0x1f));
    }
    return 0;
}
void hook_renderscene(void* param_1, uintptr_t param_2, uintptr_t param_3, uint32_t param_4, uint32_t param_5,
    uint32_t param_6, uint32_t param_7, uint32_t param_8) {

	//iterating ground items, simple enough
    const auto item_lists = reinterpret_cast<osrs::linked_list_first*>(osrs::offsets::ground_item_list);
    for (int f = 0; f < 4; f++) {
        for (int x = 0; x < 104; x++) {
            for (int y = 0; y < 104; y++) {
                auto& item_ll = item_lists[f * 104 * 104 + x * 104 + y];

                auto ptr = item_ll.start_of_list;
                int height = 0;
                while (reinterpret_cast<uintptr_t>(ptr) != reinterpret_cast<uintptr_t>(&item_ll.end_of_list)) {
                    osrs::vec2 screenpos;
                    std::cout << std::format("ground item: {} ({})\n", ptr->object->item_id, ptr->object->item_count);;
                    ptr = ptr->next;
                }
            }
        }
    }
    osrs::player* local_player = *reinterpret_cast<osrs::player**>(osrs::offsets::local_player);

    //iterating players and npcs
    const int player_count = *reinterpret_cast<int*>(osrs::offsets::player_count);
    const auto index_array = reinterpret_cast<int*>(osrs::offsets::player_indices);
    const auto player_array = reinterpret_cast<osrs::osrs_ptr*>(osrs::offsets::player_list);
    if (index_array && local_player && player_array)
    {
        for (int i = 0; i < player_count; i++) {
            const auto plr = static_cast<osrs::player*>(player_array[index_array[i]].ptr_object);
            if (!plr)
                continue;
            if (plr->def)
                std::cout << std::format("{} ({},{}) lvl-{}\n", plr->def->name, plr->position_x, plr->position_y, plr->combat_level());
        }
    }

    
	
    const int npc_count = *reinterpret_cast<int*>(osrs::offsets::npc_count);
    const int* npc_index_array = *reinterpret_cast<int**>(osrs::offsets::npc_indices);
    const auto npc_object_array = reinterpret_cast<osrs::osrs_ptr*>(osrs::offsets::npc_list);
    for (int i = 0; i < npc_count; i++) {
        auto npc_obj = static_cast<osrs::npc*>(npc_object_array[npc_index_array[i]].ptr_object);
        if (!npc_obj)
            continue;

        if (npc_obj->def)
            std::cout << std::format("{} ({},{}) lvl-{}\n", npc_obj->def->name, npc_obj->position_x, npc_obj->position_y, npc_obj->def->combat_lvl);
    }
    
    // varbits are neat, usually contain game state information
    std::cout << std::format("opponent hp: {}/{}\n", read_varb(6099), read_varb(6100));


	//basic item container use, banks, inventories, stores, gear use this stuff.
    auto items = reinterpret_cast<osrs::item_containers*>(osrs::offsets::item_containers);

    items->containers[osrs::CONTAINER_INVENTORY]->find_first(1511); // finds 1511 in inventory if present
	
    auto mouseover_data = *reinterpret_cast<osrs::mouseover**>(osrs::offsets::mouseover); // contains miscellaneous data about current mouse action you're hovering


	
    auto scene_data = *reinterpret_cast<osrs::scene**>(osrs::offsets::scene);
    if (scene_data && scene_data->tile_array && local_player)
    {
        for (int f = 0; f < 4; f++) { //we iterate objects around the player from the scene in this loop, we do nothing but it's an example
            for (int x = std::clamp((local_player->position_x >> 7) - 25, 0, scene_data->scene_x); x < std::clamp(
                (local_player->position_x >> 7) + 25, 0, scene_data->scene_x); x++) {
                for (int y = std::clamp((local_player->position_y >> 7) - 25, 0, scene_data->scene_y); y < std::clamp(
                    (local_player->position_y >> 7) + 25, 0, scene_data->scene_y); y++) {

                    osrs::scene_tile* tile = scene_data->tile_array[f * scene_data->scene_x * scene_data->scene_y + x *
                        scene_data->scene_x +
                        y].tile;
                    if (!tile) // this is null if theres no tile on the x,y,z coordinate. example on floor 1 if there is no two story buildings at that place
                        continue;

                    uint8_t* tileflags = *reinterpret_cast<uint8_t**>(osrs::offsets::tileflags); // tile flag tells if a tile can be walked on or if it has something on it e.g. bridge or ge tile

                    if ((tileflags[f * scene_data->scene_x * scene_data->scene_y + x * scene_data->scene_x + y] & 0x1)
                        == 1 && (tileflags[(f + 1) * scene_data->scene_x * scene_data->scene_y + x * scene_data->scene_x
                            + y] & 0x2) == 0) {

                    	// can not be walked on and the level above doesn't have a bridge on it, which would override not being able to walk

                    }
                    if (tile->attached) {
                    	//"attached" is a misnomer, partially blocking objects that block some directions but can be walked on. such as walls or doors
                        uint32_t object_id = tile->attached->long_id >> (0x14 & 0x3f);
                        if (object_id) {
                        	// Here we fix "MultiObjects". Stuff which changes state based on game state such as DMM booths or hidey holes etc. messy, wrap if you want to
                        	// similar needs to be done for npcs but not demonstrated
                            auto definition_ptr = static_cast<osrs::osrs_ptr*>(osrs::functions::get_object_definition(
                                object_id));
                            if (definition_ptr) {
                                auto object_def = static_cast<osrs::object_definition*>(definition_ptr->ptr_object);
                                if (object_def && object_def->version_array != nullptr) {
                                    definition_ptr = static_cast<osrs::osrs_ptr*>(osrs::functions::fix_object_definition(
                                        object_def));
                                    if (definition_ptr)
                                        object_def = static_cast<osrs::object_definition*>(definition_ptr->ptr_object);
                                    else
                                        object_def = nullptr;
                                }
                                if (object_def) {
                                	// objects definition could be resolved and can be used for information
                                    // these objects are partially blocking like walls or doors
                                }
                            }
                        }
                    }
                    for (int i = 0; i < tile->renderable_count; i++) {
                    	//renderables are everything else, players, objects, npcs etc. in this case its used only for objects, game objects in this list are usually fully blocking.
	                    osrs::renderable_object* object = tile->renderables[i].renderable;
                        if (!object)
                            continue;
                        if (object->unk_int == 0)
                            continue;
                        uint32_t object_id = object->long_id >> (0x14 & 0x3f);
                        if (object_id) {

                            auto definition_ptr = static_cast<osrs::osrs_ptr*>(osrs::functions::get_object_definition(
                                object_id));
                            if (definition_ptr) {
                                auto object_def = static_cast<osrs::object_definition*>(definition_ptr->ptr_object);
                                if (object_def && object_def->version_array != nullptr) {
                                    definition_ptr = static_cast<osrs::osrs_ptr*>(osrs::functions::fix_object_definition(
                                        object_def));
                                    if (definition_ptr)
                                        object_def = static_cast<osrs::object_definition*>(definition_ptr->ptr_object);
                                    else
                                        object_def = nullptr;
                                }
                                if (object_def) {
                                    //these are all other objects in the renderlist
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    auto iface = reinterpret_cast<osrs::interface_list*>(osrs::offsets::interface_list);
	
    PLH::FnCast(renderscene_tramp, osrs::functions::fn_render_scene())(param_1, param_2, param_3, param_4,
        param_5, param_6, param_7, param_8);
}
bool attach_debug_console() {
    FILE* g_pConStream;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole())
            return false;
    }

    if (!SetConsoleTitleA("DebugMessages"))
        return false;

    errno_t err = freopen_s(&g_pConStream, "CONOUT$", "w", stdout);

    if (err != 0)
        return false;

    return true;
}
void init_hack() {
    auto osrs_base = reinterpret_cast<uint64_t>(GetModuleHandleA("osclient.exe"));

    osrs::functions::init_offsets(osrs_base);
    osrs::offsets::init_offsets(osrs_base);
    UnhookWindowsHookEx(*(HHOOK*)osrs::offsets::window_hook); //unhook the windowhook so you can crash/debug without your mouse getting fucked up
	
    if (*reinterpret_cast<int*>(osrs::offsets::game_state) == 10 && // if injecting in login screen you will be switched to regular login instead of steam
        *reinterpret_cast<int*>(osrs::offsets::login_state) == 0) {
        *reinterpret_cast<int*>(osrs::offsets::login_state) = 1;
    }


    disasm = new PLH::CapstoneDisassembler(PLH::Mode::x64);
    //these may fail if heap fragmentation does tricks on you. you can use hook->setDetourScheme to use codecave or inplace if needed or make code that falls back automatically if hook returns 0.
    renderscene_hook = new PLH::x64Detour(reinterpret_cast<uint64_t>(osrs::functions::render_scene),
        reinterpret_cast<uint64_t>(&hook_renderscene),
        &renderscene_tramp, *disasm);
	
    renderscene_hook->hook();

    doaction_hook = new PLH::x64Detour(reinterpret_cast<uint64_t>(osrs::functions::doaction),
        reinterpret_cast<uint64_t>(&hook_do_action), &doaction_tramp,
        *disasm);
    doaction_hook->hook();

    projectile_hook = new PLH::x64Detour(reinterpret_cast<uint64_t>(osrs::functions::projectiles),
        reinterpret_cast<uint64_t>(&hook_projectiles), &projectile_tramp,
        *disasm);
    projectile_hook->hook();

}
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        attach_debug_console();
        CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(init_hack), nullptr, 0, nullptr);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

