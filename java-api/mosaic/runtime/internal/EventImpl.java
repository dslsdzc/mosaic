package mosaic.runtime.internal;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import mosaic.runtime.MosaicEvent;
import mosaic.runtime.MosaicEventCatalog;
import mosaic.runtime.MosaicEventDispatcher;
import mosaic.runtime.MosaicEventHandler;
import mosaic.runtime.MosaicEventSubscription;

/** 事件域实现:派发 = C 内核订阅者(Native.eventDispatch)+ Java 侧订阅表;
 *  目录 = events.h 事件名常量表(数量以 C 目录为准:契约测试经
 *  Native.eventCatalogName 探测总数与 EVENT_NAMES 双向比对)。 */
public final class EventImpl implements MosaicEventDispatcher {
    private final RuntimeImpl rt;
    private final Map<Integer, List<MosaicEventHandler>> handlers = new HashMap<>();

    EventImpl(RuntimeImpl rt) { this.rt = rt; }

    public int dispatch(int eventId, byte[] payload) {
        int n = Native.eventDispatch(rt.handle(), eventId, payload);
        List<MosaicEventHandler> list;
        synchronized (handlers) {
            list = handlers.get(eventId);
            if (list == null) return n;
            list = new ArrayList<>(list);
        }
        for (MosaicEventHandler h : list) h.onEvent(eventId, payload);
        return n + list.size();
    }

    public MosaicEventSubscription subscribe(int eventId, MosaicEventHandler handler) {
        if (handler == null) throw new NullPointerException("handler");
        synchronized (handlers) {
            handlers.computeIfAbsent(eventId, k -> new ArrayList<>()).add(handler);
        }
        return new SubscriptionImpl(eventId, handler);
    }

    public void unsubscribe(MosaicEventSubscription subscription) {
        if (!(subscription instanceof SubscriptionImpl)) return;
        SubscriptionImpl s = (SubscriptionImpl) subscription;
        synchronized (handlers) {
            List<MosaicEventHandler> list = handlers.get(s.eventId);
            if (list != null) list.remove(s.handler);
        }
    }

    MosaicEventCatalog catalog() { return new EventCatalogImpl(rt); }

    /* ---- 订阅句柄(非静态:close 回链到 EventImpl.unsubscribe) ---- */
    private final class SubscriptionImpl implements MosaicEventSubscription {
        final int eventId;
        final MosaicEventHandler handler;
        SubscriptionImpl(int eventId, MosaicEventHandler handler) {
            this.eventId = eventId;
            this.handler = handler;
        }
        public int eventId() { return eventId; }
        public void close() { unsubscribe(this); }
    }

    /* ---- 目录(events.h 名字抄录——双端比对基准,数量派生自 C 目录
           (Native.eventCatalogName 探测),不硬编码;注册态经 Native.eventId 探测) ---- */
    static final class EventCatalogImpl implements MosaicEventCatalog {
        private final RuntimeImpl rt;
        EventCatalogImpl(RuntimeImpl rt) { this.rt = rt; }

        /** events.h 目录按名升序(名字唯一 → 与 strcmp 序一致;Java 二分同序)。 */
        static final String[] EVENT_NAMES = {
            "block_anvil_break", "block_anvil_repair", "block_bell_ring", "block_break",
            "block_brew", "block_brew_fuel", "block_burn", "block_cauldron_level_change",
            "block_crack", "block_crop_grow", "block_damage", "block_dispense",
            "block_drop_exp", "block_drop_item", "block_explode", "block_fade",
            "block_fertilize", "block_form", "block_from_to", "block_grow",
            "block_ignite", "block_interact", "block_leaves_decay", "block_moisture_change",
            "block_multi_place", "block_note_play", "block_peel", "block_physics",
            "block_piston_extend", "block_piston_retract", "block_place", "block_redstone",
            "block_sign_change", "block_sponge_absorb", "block_spread", "block_structure_grow",
            "block_temperature_change", "block_tick", "chunk_entities_load",
            "chunk_entities_unload", "chunk_generate", "chunk_load", "chunk_populate",
            "chunk_pre_generate", "chunk_save", "chunk_unload", "dimension_load",
            "dimension_unload", "entity_air_change", "entity_arrow_nock",
            "entity_bat_toggle_sleep", "entity_block_form", "entity_break_door",
            "entity_change_block", "entity_combust", "entity_creeper_power", "entity_damage",
            "entity_damage_by_block", "entity_damage_by_entity", "entity_death",
            "entity_despawn", "entity_dismount", "entity_drop_item", "entity_dye",
            "entity_enter_block", "entity_enter_vehicle", "entity_exit_vehicle",
            "entity_explode", "entity_fall", "entity_food_level_change",
            "entity_hanging_break", "entity_hanging_place", "entity_horse_jump",
            "entity_interact", "entity_liquid_splash", "entity_mount", "entity_pickup_item",
            "entity_portal", "entity_portal_enter", "entity_portal_exit",
            "entity_potion_effect", "entity_potion_splash", "entity_projectile_hit",
            "entity_projectile_launch", "entity_regain_health", "entity_remove",
            "entity_resurrect", "entity_shear", "entity_shoot_bow", "entity_slime_split",
            "entity_spawn", "entity_tame", "entity_target", "entity_target_living",
            "entity_teleport", "entity_tick", "entity_tnt_prime", "entity_transform",
            "entity_unleash", "entity_vehicle_create", "entity_vehicle_damage",
            "entity_vehicle_destroy", "entity_vehicle_move", "entity_villager_acquire_trade",
            "entity_villager_career_change", "entity_villager_repair", "inventory_change",
            "inventory_drag", "item_break", "item_craft", "item_damage", "item_despawn",
            "item_drop", "item_durability_change", "item_enchant", "item_lore_change",
            "item_mend", "item_merge", "item_move", "item_pickup", "item_rename",
            "item_smelt", "item_spawn", "item_swap_hand", "item_transform", "item_use",
            "lightning_strike", "player_advancement", "player_armor_change",
            "player_bed_enter", "player_bed_leave", "player_break_item",
            "player_bucket_empty", "player_bucket_fill", "player_chat", "player_command",
            "player_command_preprocess", "player_command_send", "player_consume_item",
            "player_death", "player_drop_item", "player_edit_book", "player_egg_throw",
            "player_exhaustion", "player_exp_change", "player_fish",
            "player_food_level_change", "player_game_mode_change", "player_harvest_block",
            "player_interact", "player_interact_at_entity", "player_inventory_click",
            "player_inventory_close", "player_inventory_open", "player_item_held_change",
            "player_join", "player_kick", "player_leash_entity", "player_leave",
            "player_level_change", "player_locale_change", "player_login", "player_move",
            "player_pickup_arrow", "player_pickup_item", "player_portal",
            "player_pre_login", "player_resource_pack_status", "player_respawn",
            "player_shear_entity", "player_statistic", "player_swing_arm",
            "player_tame_entity", "player_teleport", "player_toggle_flight",
            "player_toggle_sneak", "player_toggle_sprint", "player_trade",
            "player_unleash_entity", "player_velocity", "player_world_change",
            "portal_break", "portal_create", "raid_spawn_wave", "raid_stop",
            "server_broadcast", "server_command", "server_command_send",
            "server_list_ping", "server_plugin_disable", "server_plugin_enable",
            "server_start", "server_stop", "server_whitelist_toggle", "spawner_spawn",
            "tick", "time_change", "weather_change", "weather_thunder", "world_init",
            "world_load", "world_save", "world_spawn_change", "world_time_skip",
            "world_unload",
        };

        /** 载荷大小 = events.h 域结构体:player=4B、block/entity=20B、item=12B、
         *  server=0(空)、其余(world 周期等)= 4B。 */
        static int payloadSize(String name) {
            if (name.startsWith("player_")) return 4;
            if (name.startsWith("block_")) return 20;
            if (name.startsWith("item_") || name.startsWith("inventory_")) return 12;
            if (name.startsWith("entity_")) return 20;
            if (name.startsWith("server_")) return 0;
            return 4;
        }

        public MosaicEvent find(String name) {
            if (name == null) return null;
            int lo = 0, hi = EVENT_NAMES.length;
            while (lo < hi) {
                int mid = (lo + hi) >>> 1;
                int c = name.compareTo(EVENT_NAMES[mid]);
                if (c == 0) {
                    int id = Native.eventId(rt.handle(), name);
                    return id >= 0 ? new EventEntryImpl(id, name, payloadSize(name)) : null;
                }
                if (c < 0) hi = mid; else lo = mid + 1;
            }
            return null;
        }

        public int count() {
            int n = 0;
            for (String name : EVENT_NAMES)
                if (Native.eventId(rt.handle(), name) >= 0) n++;
            return n;
        }
    }

    /** 目录条目(未注册态由 catalog.find 拦截,不产生实例)。 */
    static final class EventEntryImpl implements MosaicEvent {
        private final int eventId;
        private final String name;
        private final int payloadSize;
        EventEntryImpl(int eventId, String name, int payloadSize) {
            this.eventId = eventId; this.name = name; this.payloadSize = payloadSize;
        }
        public int eventId() { return eventId; }
        public String name() { return name; }
        public int payloadSize() { return payloadSize; }
    }
}
