/* src/events.c — M3-1:事件类型 API v1 目录(名字 + 频率档 + 载荷签名)。
   事件类型本身就是公开 API;目录按名字升序(长度感知序——memcmp 前缀 +
   长度 tiebreak,与 builder 事件名排序、运行时二分同一语义;名字唯一故与
   strcmp 序一致)。按域组织在每行注释中标注。 */
#include "mosaic/events.h"
#include <string.h>

/* 长度感知比较(与 builder ev_cmp / 运行时二分同序) */
static int ev_name_cmp(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  size_t c = la < lb ? la : lb;
  int r = memcmp(a, b, c);
  if (r) return r;
  return (int)la - (int)lb;
}

const mosaic_ev_spec mosaic_events_catalog[] = {
  { "block_anvil_break", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_anvil_repair", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_bell_ring", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_break", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_brew", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_brew_fuel", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_burn", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_cauldron_level_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_crack", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_crop_grow", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_damage", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_dispense", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_drop_exp", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_drop_item", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_explode", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_fade", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_fertilize", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_form", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_from_to", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_grow", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_ignite", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_interact", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_leaves_decay", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_moisture_change", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_multi_place", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_note_play", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_peel", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_physics", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_piston_extend", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_piston_retract", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_place", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_redstone", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_sign_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_sponge_absorb", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_spread", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "block_structure_grow", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_block) },   /* block */
  { "block_temperature_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_block) },   /* block */
  { "block_tick", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_block) },   /* block */
  { "chunk_entities_load", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "chunk_entities_unload", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "chunk_generate", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "chunk_load", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_tick) },   /* world */
  { "chunk_populate", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "chunk_pre_generate", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "chunk_save", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_tick) },   /* world */
  { "chunk_unload", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_tick) },   /* world */
  { "dimension_load", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "dimension_unload", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "entity_air_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_arrow_nock", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_bat_toggle_sleep", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_block_form", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_break_door", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_change_block", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_combust", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_creeper_power", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_damage", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_damage_by_block", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_damage_by_entity", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_death", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_despawn", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_dismount", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_drop_item", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_dye", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_enter_block", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_enter_vehicle", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_exit_vehicle", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_explode", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_fall", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_food_level_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_hanging_break", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_hanging_place", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_horse_jump", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_interact", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_liquid_splash", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_mount", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_pickup_item", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_portal", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_portal_enter", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_portal_exit", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_potion_effect", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_potion_splash", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_projectile_hit", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_projectile_launch", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_regain_health", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_remove", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_resurrect", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_shear", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_shoot_bow", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_slime_split", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_spawn", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_tame", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_target", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_target_living", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_teleport", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_tick", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_tnt_prime", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_transform", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_unleash", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_vehicle_create", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_vehicle_damage", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_vehicle_destroy", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_vehicle_move", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_villager_acquire_trade", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_villager_career_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "entity_villager_repair", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_entity) },   /* entity */
  { "inventory_change", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_item) },   /* item */
  { "inventory_drag", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_break", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_craft", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_damage", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_despawn", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_drop", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_durability_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_enchant", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_lore_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_mend", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_merge", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_move", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_pickup", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_rename", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_smelt", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_spawn", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_swap_hand", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_item) },   /* item */
  { "item_transform", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_item) },   /* item */
  { "item_use", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_item) },   /* item */
  { "lightning_strike", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "player_advancement", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_armor_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_bed_enter", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_bed_leave", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_break_item", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_bucket_empty", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_bucket_fill", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_chat", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_command", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_command_preprocess", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_command_send", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_consume_item", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_death", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_drop_item", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_edit_book", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_egg_throw", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_exhaustion", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_exp_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_fish", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_food_level_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_game_mode_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_harvest_block", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_interact", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_interact_at_entity", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_inventory_click", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_inventory_close", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_inventory_open", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_item_held_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_join", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_kick", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_leash_entity", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_leave", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_level_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_locale_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_login", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_move", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_pickup_arrow", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_pickup_item", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_portal", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_pre_login", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_resource_pack_status", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_respawn", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_shear_entity", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_statistic", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_swing_arm", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_tame_entity", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_teleport", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_toggle_flight", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "player_toggle_sneak", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_toggle_sprint", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_player) },   /* player */
  { "player_trade", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_unleash_entity", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_velocity", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_player) },   /* player */
  { "player_world_change", MOSAIC_EV_FREQ_MID, sizeof(mosaic_ev_player) },   /* player */
  { "portal_break", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "portal_create", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "raid_spawn_wave", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "raid_stop", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "server_broadcast", MOSAIC_EV_FREQ_MID, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_command", MOSAIC_EV_FREQ_MID, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_command_send", MOSAIC_EV_FREQ_MID, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_list_ping", MOSAIC_EV_FREQ_LOW, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_plugin_disable", MOSAIC_EV_FREQ_LOW, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_plugin_enable", MOSAIC_EV_FREQ_LOW, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_start", MOSAIC_EV_FREQ_LOW, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_stop", MOSAIC_EV_FREQ_LOW, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "server_whitelist_toggle", MOSAIC_EV_FREQ_LOW, 0 /* 无载荷(mosaic_ev_empty) */ },   /* service */
  { "spawner_spawn", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "tick", MOSAIC_EV_FREQ_HIGH, sizeof(mosaic_ev_tick) },   /* world */
  { "time_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "weather_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "weather_thunder", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "world_init", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "world_load", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "world_save", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "world_spawn_change", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "world_time_skip", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
  { "world_unload", MOSAIC_EV_FREQ_LOW, sizeof(mosaic_ev_tick) },   /* world */
};
const u32 mosaic_events_catalog_count =
    (u32)(sizeof(mosaic_events_catalog) / sizeof(mosaic_events_catalog[0]));

const mosaic_ev_spec *mosaic_event_spec_by_name(const char *name) {
  if (!name) return NULL;
  u32 lo = 0, hi = mosaic_events_catalog_count;
  while (lo < hi) {
    u32 mid = lo + (hi - lo) / 2;
    int r = ev_name_cmp(name, mosaic_events_catalog[mid].name);
    if (r == 0) return &mosaic_events_catalog[mid];
    if (r < 0) hi = mid; else lo = mid + 1;
  }
  return NULL;
}
