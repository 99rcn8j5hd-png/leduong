// offsets.hpp — mọi offset đọc từ file cấu hình, KHÔNG hardcode trong code.
// dump_offsets sinh ra file này; provider chỉ việc nạp.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace hs {

struct Offsets {
    // --- global ---
    uint64_t gworld   = 0;   // image_base + gworld  -> UWorld*
    uint64_t gobjects = 0;   // image_base + gobjects -> TUObjectArray
    uint64_t gnames   = 0;   // image_base + gnames  -> FNameEntry** / FNamePool
    uint32_t gnames_deref   = 0;  // 1 = phải deref thêm 1 lần mới tới bảng block
    uint32_t gobjects_deref = 0;  // 1 = global trỏ tới FUObjectArray, Objects ở +0

    // --- object model (UE4) ---
    uint32_t uobj_class      = 0x18; // UObject::ClassPrivate
    uint32_t uobj_name_index = 0x20; // UObject::NamePrivate.Index

    // --- world chain ---
    uint32_t persist_level = 0;  // UWorld::PersistentLevel
    uint32_t level_actors  = 0;  // ULevel::Actors (TArray<AActor*>)
    uint32_t actor_root    = 0;  // AActor::RootComponent
    uint32_t comp_xform    = 0;  // USceneComponent::ComponentToWorld (FTransform)
    uint32_t pc_pawn       = 0;  // APlayerController::AcknowledgedPawn
    uint32_t pawn_player_state = 0;
    uint32_t pawn_mesh     = 0;
    uint32_t mesh_bones    = 0;  // USkeletalMeshComponent::CachedBoneSpaceTransforms (TArray<FTransform>)
    uint32_t ps_team       = 0;
    uint32_t ps_health     = 0;
    uint32_t ps_name       = 0;  // APlayerState::PlayerNamePrivate (FString)
    uint32_t pc_camera_mgr = 0;  // APlayerController::PlayerCameraManager (null ở PC remote)
    uint32_t cam_pov_loc   = 0;  // APlayerCameraManager::CameraCachePrivate.POV.Location

    // --- FName layout ---
    uint32_t name_block_shift  = 14;     // idx >> shift = block
    uint32_t name_block_entries= 16384;  // entry / block
    uint32_t name_entry_stride = 8;      // byte / entry slot (8 = TNameEntryArray 4.18)
    uint32_t name_entry_hdr    = 8;      // byte header trước chuỗi (4.18: Index+HashNext)

    // --- GObjects layout ---
    uint32_t obj_chunk_shift = 13;   // i >> shift = chunk
    uint32_t obj_item_stride = 0x10; // sizeof(FUObjectItem)
    uint32_t obj_item_object = 0;    // offset của UObject* trong FUObjectItem
    uint32_t obj_num_offset  = 0xC;  // TUObjectArray::NumElements

    // --- bone ---
    int32_t bone_head   = -1;
    int32_t bone_neck   = -1;
    int32_t bone_root   = -1;

    // --- meta ---
    std::string image   = "";   // tên image exec ("" = tự tìm)
    std::string game    = "pubgm";
    std::string version = "4.6.0";
    uint64_t    dumped_at = 0;

    bool valid() const {
        return gworld && persist_level && level_actors && actor_root && comp_xform &&
               gobjects && gnames && name_entry_stride;
    }

    // JSON tối giản, đủ để người đọc và máy đọc (không cần thư viện ngoài)
    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto num = [&](const char* key, uint64_t dflt) -> uint64_t {
            std::string k = std::string("\"") + key + "\"";
            size_t p = buf.find(k);
            if (p == std::string::npos) return dflt;
            p = buf.find(':', p);
            if (p == std::string::npos) return dflt;
            return (uint64_t)strtoull(buf.c_str() + p + 1, nullptr, 0);
        };
        auto str = [&](const char* key, const std::string& dflt) -> std::string {
            std::string k = std::string("\"") + key + "\"";
            size_t p = buf.find(k);
            if (p == std::string::npos) return dflt;
            size_t q1 = buf.find('"', buf.find(':', p) + 1);
            if (q1 == std::string::npos) return dflt;
            size_t q2 = buf.find('"', q1 + 1);
            if (q2 == std::string::npos) return dflt;
            return buf.substr(q1 + 1, q2 - q1 - 1);
        };

        gworld               = num("gworld", gworld);
        gobjects             = num("gobjects", gobjects);
        gnames               = num("gnames", gnames);
        gnames_deref         = (uint32_t)num("gnames_deref", gnames_deref);
        gobjects_deref       = (uint32_t)num("gobjects_deref", gobjects_deref);
        uobj_class           = (uint32_t)num("uobj_class", uobj_class);
        uobj_name_index      = (uint32_t)num("uobj_name_index", uobj_name_index);
        persist_level        = (uint32_t)num("persist_level", persist_level);
        level_actors         = (uint32_t)num("level_actors", level_actors);
        actor_root           = (uint32_t)num("actor_root", actor_root);
        comp_xform           = (uint32_t)num("comp_xform", comp_xform);
        pc_pawn              = (uint32_t)num("pc_pawn", pc_pawn);
        pawn_player_state    = (uint32_t)num("pawn_player_state", pawn_player_state);
        pawn_mesh            = (uint32_t)num("pawn_mesh", pawn_mesh);
        mesh_bones           = (uint32_t)num("mesh_bones", mesh_bones);
        ps_team              = (uint32_t)num("ps_team", ps_team);
        ps_health            = (uint32_t)num("ps_health", ps_health);
        ps_name              = (uint32_t)num("ps_name", ps_name);
        pc_camera_mgr        = (uint32_t)num("pc_camera_mgr", pc_camera_mgr);
        cam_pov_loc          = (uint32_t)num("cam_pov_loc", cam_pov_loc);
        name_block_shift     = (uint32_t)num("name_block_shift", name_block_shift);
        name_block_entries   = (uint32_t)num("name_block_entries", name_block_entries);
        name_entry_stride    = (uint32_t)num("name_entry_stride", name_entry_stride);
        name_entry_hdr       = (uint32_t)num("name_entry_hdr", name_entry_hdr);
        obj_chunk_shift      = (uint32_t)num("obj_chunk_shift", obj_chunk_shift);
        obj_item_stride      = (uint32_t)num("obj_item_stride", obj_item_stride);
        obj_item_object      = (uint32_t)num("obj_item_object", obj_item_object);
        obj_num_offset       = (uint32_t)num("obj_num_offset", obj_num_offset);
        bone_head            = (int32_t)num("bone_head", (uint64_t)(bone_head < 0 ? 0 : bone_head)) ;
        bone_neck            = (int32_t)num("bone_neck", (uint64_t)(bone_neck < 0 ? 0 : bone_neck));
        bone_root            = (int32_t)num("bone_root", (uint64_t)(bone_root < 0 ? 0 : bone_root));
        image                = str("image", image);
        game                 = str("game", game);
        version              = str("version", version);
        dumped_at            = num("dumped_at", dumped_at);
        return true;
    }

    bool save(const std::string& path) const {
        FILE* f = fopen(path.c_str(), "w");
        if (!f) return false;
        fprintf(f,
            "{\n"
            "  \"game\": \"%s\",\n  \"version\": \"%s\",\n  \"image\": \"%s\",\n"
            "  \"dumped_at\": %llu,\n\n"
            "  \"gworld\": %llu,\n  \"gobjects\": %llu,\n  \"gnames\": %llu,\n"
            "  \"gnames_deref\": %u,\n  \"gobjects_deref\": %u,\n\n"
            "  \"uobj_class\": %u,\n  \"uobj_name_index\": %u,\n\n"
            "  \"persist_level\": %u,\n  \"level_actors\": %u,\n  \"actor_root\": %u,\n"
            "  \"comp_xform\": %u,\n  \"pc_pawn\": %u,\n  \"pawn_player_state\": %u,\n"
            "  \"pawn_mesh\": %u,\n  \"mesh_bones\": %u,\n  \"ps_team\": %u,\n  \"ps_health\": %u,\n  \"ps_name\": %u,\n  \"pc_camera_mgr\": %u,\n  \"cam_pov_loc\": %u,\n\n"
            "  \"name_block_shift\": %u,\n  \"name_block_entries\": %u,\n"
            "  \"name_entry_stride\": %u,\n  \"name_entry_hdr\": %u,\n\n"
            "  \"obj_chunk_shift\": %u,\n  \"obj_item_stride\": %u,\n"
            "  \"obj_item_object\": %u,\n  \"obj_num_offset\": %u,\n\n"
            "  \"bone_head\": %d,\n  \"bone_neck\": %d,\n  \"bone_root\": %d\n"
            "}\n",
            game.c_str(), version.c_str(), image.c_str(), (unsigned long long)dumped_at,
            (unsigned long long)gworld, (unsigned long long)gobjects, (unsigned long long)gnames,
            gnames_deref, gobjects_deref,
            uobj_class, uobj_name_index,
            persist_level, level_actors, actor_root, comp_xform, pc_pawn,
            pawn_player_state, pawn_mesh, mesh_bones, ps_team, ps_health, ps_name,
            pc_camera_mgr, cam_pov_loc,
            name_block_shift, name_block_entries, name_entry_stride, name_entry_hdr,
            obj_chunk_shift, obj_item_stride, obj_item_object, obj_num_offset,
            bone_head, bone_neck, bone_root);
        fclose(f);
        return true;
    }
};

} // namespace hs
