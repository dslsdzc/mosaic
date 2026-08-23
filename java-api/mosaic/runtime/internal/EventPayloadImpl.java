package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicEventPayload;

/** 事件载荷类型化解码(API 设计规格 §8):byte[] 小端 ↔ include/mosaic/events.h
 *  载荷结构体。域判定 = eventId → EVENT_NAMES 名称前缀(与 EventCatalogImpl.
 *  payloadSize 同一规则);解码失败(长度与域不符 / 字节数非 4 倍数 / eventId
 *  无运行时注册)→ MosaicHandleException(§8:"解码失败 → 句柄异常")。
 *
 *  eventId 语义:C 内核事件 id = 包内事件表排序位置(见 runtime.c
 *  mosaic_runtime_event_id),对全量目录 pack 恰为 EVENT_NAMES 下标;对子集
 *  pack(测试包)必须经活跃运行时探测 Native.eventId 反查名称。
 *
 *  domains(events.h 载荷结构体,字段序 = 结构体声明序):
 *    player_*    → mosaic_ev_player {player_id}                    4B
 *    player_command → mosaic_ev_player_command {player_id, cmd_hash} 8B
 *    block_*     → mosaic_ev_block  {player_id, x, y, z, block_type} 20B
 *    item_* 与 inventory_* → mosaic_ev_item {player_id, item_id, slot} 12B
 *    entity_*    → mosaic_ev_entity {entity_id, entity_type, x, y, z,
 *                                    dimension, source}            28B
 *    server_*    → mosaic_ev_empty {}                               0B
 *    其余(世界周期:tick、world_*、chunk_*、time_change 等)
 *                → mosaic_ev_tick  {tick_no}                        4B */
public final class EventPayloadImpl implements MosaicEventPayload {

    /** 载荷域:大小与字段数 = events.h 结构体;与 EventCatalogImpl.payloadSize
     *  启发式一致(player=4/player_command=8/block=20/entity=28/
     *  item·inventory=12/server=0/其余 4)。 */
    private enum Domain {
        PLAYER(4, 1), PLAYER_CMD(8, 2), BLOCK(20, 5), ITEM(12, 3),
        ENTITY(28, 7), TICK(4, 1), EMPTY(0, 0);
        final int size;
        final int count;
        Domain(int size, int count) { this.size = size; this.count = count; }
    }

    private final int eventId;
    private final Domain domain;
    private final int[] fields;

    private EventPayloadImpl(int eventId, Domain domain, int[] fields) {
        this.eventId = eventId;
        this.domain = domain;
        this.fields = fields;
    }

    /** 静态工厂:按事件域解码(构造即解码;失败抛 MosaicHandleException)。 */
    public static MosaicEventPayload of(int eventId, byte[] raw) {
        if (raw == null) throw new MosaicHandleException("event payload is null");
        String name = resolveName(eventId);
        Domain d = domainOf(name);
        checkLength(d, raw);
        return new EventPayloadImpl(eventId, d, decodeLe(d, raw));
    }

    /** 类型化解码:按域结构体字段序返回 u32 数组(构造已解码;副本返回)。
     *  失败语义见类注释(工厂即解码入口,失败在 of 处抛句柄异常)。 */
    public int[] decodeInts() {
        return fields.clone();
    }

    /** 编码:字段 → 小端 byte[](回环:of(id, b).encode() == b)。 */
    public byte[] encode() {
        byte[] out = new byte[fields.length * 4];
        for (int i = 0; i < fields.length; i++) {
            int v = fields[i], o = i * 4;
            out[o] = (byte) v;
            out[o + 1] = (byte) (v >>> 8);
            out[o + 2] = (byte) (v >>> 16);
            out[o + 3] = (byte) (v >>> 24);
        }
        return out;
    }

    /* ---- 域判定 / 名称反查 / 解码 ---- */

    /** 域判定 = 名称前缀(与 EventCatalogImpl.payloadSize 同一规则,七域全覆盖;
     *  player_command 特判须在 player_ 前缀之前)。 */
    static Domain domainOf(String name) {
        if (name.equals("player_command")) return Domain.PLAYER_CMD;
        if (name.startsWith("player_")) return Domain.PLAYER;
        if (name.startsWith("block_")) return Domain.BLOCK;
        if (name.startsWith("item_") || name.startsWith("inventory_")) return Domain.ITEM;
        if (name.startsWith("entity_")) return Domain.ENTITY;
        if (name.startsWith("server_")) return Domain.EMPTY;
        return Domain.TICK; /* 其余(含 tick、time_change、weather_*、chunk_*、world_* 等)= 世界周期 4B */
    }

    /** eventId → 目录名。先试全量目录下标(EVENT_NAMES[eventId])并验证该名在
     *  活跃运行时确以该 id 注册(生产 pack 事件表 = 全量目录,一次探测命中);
     *  未命中则探测全部 EVENT_NAMES(子集 pack 的 id 是包内排序位置,必须探测)。
     *  任何运行时都未注册该 id → MosaicHandleException。 */
    static String resolveName(int eventId) {
        if (eventId < 0) throw new MosaicHandleException("unknown event id " + eventId);
        String[] names = EventImpl.EventCatalogImpl.EVENT_NAMES;
        if (eventId < names.length) {
            String fast = names[eventId];
            for (RuntimeImpl rt : RuntimeImpl.live())
                if (Native.eventId(rt.handle(), fast) == eventId) return fast;
        }
        for (String name : names)
            for (RuntimeImpl rt : RuntimeImpl.live())
                if (Native.eventId(rt.handle(), name) == eventId) return name;
        throw new MosaicHandleException("unknown event id " + eventId);
    }

    /** 失败语义(§8):长度与域不符或字节数非 4 倍数 → MosaicHandleException。 */
    static void checkLength(Domain d, byte[] raw) {
        if (raw.length % 4 != 0 || raw.length != d.size)
            throw new MosaicHandleException("event payload length " + raw.length
                + " does not match " + d + " domain (" + d.size + "B)");
    }

    /** 小端 u32 解码(与 events.h 结构体 4 对齐字段一致)。 */
    static int[] decodeLe(Domain d, byte[] raw) {
        int[] out = new int[d.count];
        for (int i = 0; i < out.length; i++) {
            int o = i * 4;
            out[i] = (raw[o] & 0xff) | ((raw[o + 1] & 0xff) << 8)
                   | ((raw[o + 2] & 0xff) << 16) | ((raw[o + 3] & 0xff) << 24);
        }
        return out;
    }

    @Override public String toString() {
        StringBuilder sb = new StringBuilder("EventPayloadImpl{eventId=").append(eventId)
            .append(", domain=").append(domain);
        for (int f : fields) sb.append(", ").append(f);
        return sb.append('}').toString();
    }
}
