package mosaic.runtime.internal;

/** 包目录常量表(网络域,Task 6):packets.c 名字抄录——N2 包目录双端一致门禁
 *  基准(契约测试经 Native.packetCatalogName 探测总数与 PACKET_NAMES 逐项
 *  双向比对,防 packets.c 与 Java 侧漂移;与 EventImpl.EVENT_NAMES 同款纪律,
 *  数量派生自 C 目录探测,不硬编码)。
 *  目录与 packets.c 完全同序:原始 168 条按名字升序,其后为 [LC-2] 追加的
 *  内嵌包变体块(7 条,块内按名升序;id 按组续号,既有 id 不动);id 分组
 *  语义见 include/mosaic/packets.h(UNKNOWN=0、PLAY_IN 0x0101..、PLAY_OUT
 *  0x0201..、LOGIN_IN 0x0501..、LOGIN_OUT 0x0601..、STATUS_IN 0x0701..、
 *  STATUS_OUT 0x0801..、HANDSHAKE_IN 0x0901..;CONFIG 组 1.20.1 为空)。
 *  运行时不用本表(agent 运行时查表用生成文件 PacketMap.java,见
 *  ci/gen_packet_map.sh);本表仅作跨语言一致性基准。 */
public final class PacketCatalogImpl {
    private PacketCatalogImpl() {}

    static final String[] PACKET_NAMES = {
            "ClientIntentionPacket", "ClientboundAddEntityPacket", "ClientboundAddExperienceOrbPacket", "ClientboundAddPlayerPacket", "ClientboundAnimatePacket", "ClientboundAwardStatsPacket",
            "ClientboundBlockChangedAckPacket", "ClientboundBlockDestructionPacket", "ClientboundBlockEntityDataPacket", "ClientboundBlockEventPacket", "ClientboundBlockUpdatePacket", "ClientboundBossEventPacket",
            "ClientboundBundlePacket", "ClientboundChangeDifficultyPacket", "ClientboundChunksBiomesPacket", "ClientboundClearTitlesPacket", "ClientboundCommandSuggestionsPacket", "ClientboundCommandsPacket",
            "ClientboundContainerClosePacket", "ClientboundContainerSetContentPacket", "ClientboundContainerSetDataPacket", "ClientboundContainerSetSlotPacket", "ClientboundCooldownPacket", "ClientboundCustomChatCompletionsPacket",
            "ClientboundCustomPayloadPacket", "ClientboundCustomQueryPacket", "ClientboundDamageEventPacket", "ClientboundDeleteChatPacket", "ClientboundDisconnectPacket", "ClientboundDisguisedChatPacket",
            "ClientboundEntityEventPacket", "ClientboundExplodePacket", "ClientboundForgetLevelChunkPacket", "ClientboundGameEventPacket", "ClientboundGameProfilePacket", "ClientboundHelloPacket",
            "ClientboundHorseScreenOpenPacket", "ClientboundHurtAnimationPacket", "ClientboundInitializeBorderPacket", "ClientboundKeepAlivePacket", "ClientboundLevelChunkWithLightPacket", "ClientboundLevelEventPacket",
            "ClientboundLevelParticlesPacket", "ClientboundLightUpdatePacket", "ClientboundLoginCompressionPacket", "ClientboundLoginDisconnectPacket", "ClientboundLoginPacket", "ClientboundMapItemDataPacket",
            "ClientboundMerchantOffersPacket", "ClientboundMoveEntityPacket", "ClientboundMoveVehiclePacket", "ClientboundOpenBookPacket", "ClientboundOpenScreenPacket", "ClientboundOpenSignEditorPacket",
            "ClientboundPingPacket", "ClientboundPlaceGhostRecipePacket", "ClientboundPlayerAbilitiesPacket", "ClientboundPlayerChatPacket", "ClientboundPlayerCombatEndPacket", "ClientboundPlayerCombatEnterPacket",
            "ClientboundPlayerCombatKillPacket", "ClientboundPlayerInfoRemovePacket", "ClientboundPlayerInfoUpdatePacket", "ClientboundPlayerLookAtPacket", "ClientboundPlayerPositionPacket", "ClientboundPongResponsePacket",
            "ClientboundRecipePacket", "ClientboundRemoveEntitiesPacket", "ClientboundRemoveMobEffectPacket", "ClientboundResourcePackPacket", "ClientboundRespawnPacket", "ClientboundRotateHeadPacket",
            "ClientboundSectionBlocksUpdatePacket", "ClientboundSelectAdvancementsTabPacket", "ClientboundServerDataPacket", "ClientboundSetActionBarTextPacket", "ClientboundSetBorderCenterPacket", "ClientboundSetBorderLerpSizePacket",
            "ClientboundSetBorderSizePacket", "ClientboundSetBorderWarningDelayPacket", "ClientboundSetBorderWarningDistancePacket", "ClientboundSetCameraPacket", "ClientboundSetCarriedItemPacket", "ClientboundSetChunkCacheCenterPacket",
            "ClientboundSetChunkCacheRadiusPacket", "ClientboundSetDefaultSpawnPositionPacket", "ClientboundSetDisplayObjectivePacket", "ClientboundSetEntityDataPacket", "ClientboundSetEntityLinkPacket", "ClientboundSetEntityMotionPacket",
            "ClientboundSetEquipmentPacket", "ClientboundSetExperiencePacket", "ClientboundSetHealthPacket", "ClientboundSetObjectivePacket", "ClientboundSetPassengersPacket", "ClientboundSetPlayerTeamPacket",
            "ClientboundSetScorePacket", "ClientboundSetSimulationDistancePacket", "ClientboundSetSubtitleTextPacket", "ClientboundSetTimePacket", "ClientboundSetTitleTextPacket", "ClientboundSetTitlesAnimationPacket",
            "ClientboundSoundEntityPacket", "ClientboundSoundPacket", "ClientboundStatusResponsePacket", "ClientboundStopSoundPacket", "ClientboundSystemChatPacket", "ClientboundTabListPacket",
            "ClientboundTagQueryPacket", "ClientboundTakeItemEntityPacket", "ClientboundTeleportEntityPacket", "ClientboundUpdateAdvancementsPacket", "ClientboundUpdateAttributesPacket", "ClientboundUpdateEnabledFeaturesPacket",
            "ClientboundUpdateMobEffectPacket", "ClientboundUpdateRecipesPacket", "ClientboundUpdateTagsPacket", "ServerboundAcceptTeleportationPacket", "ServerboundChangeDifficultyPacket", "ServerboundChatAckPacket",
            "ServerboundChatCommandPacket", "ServerboundChatPacket", "ServerboundChatSessionUpdatePacket", "ServerboundClientCommandPacket", "ServerboundClientInformationPacket", "ServerboundCommandSuggestionPacket",
            "ServerboundContainerButtonClickPacket", "ServerboundContainerClickPacket", "ServerboundContainerClosePacket", "ServerboundCustomPayloadPacket", "ServerboundCustomQueryPacket", "ServerboundEditBookPacket",
            "ServerboundHelloPacket", "ServerboundInteractPacket", "ServerboundJigsawGeneratePacket", "ServerboundKeepAlivePacket", "ServerboundKeyPacket", "ServerboundLockDifficultyPacket",
            "ServerboundMovePlayerPacket", "ServerboundMoveVehiclePacket", "ServerboundPaddleBoatPacket", "ServerboundPickItemPacket", "ServerboundPingRequestPacket", "ServerboundPlaceRecipePacket",
            "ServerboundPlayerAbilitiesPacket", "ServerboundPlayerActionPacket", "ServerboundPlayerCommandPacket", "ServerboundPlayerInputPacket", "ServerboundPongPacket", "ServerboundRecipeBookChangeSettingsPacket",
            "ServerboundRecipeBookSeenRecipePacket", "ServerboundRenameItemPacket", "ServerboundResourcePackPacket", "ServerboundSeenAdvancementsPacket", "ServerboundSelectTradePacket", "ServerboundSetBeaconPacket",
            "ServerboundSetCarriedItemPacket", "ServerboundSetCommandBlockPacket", "ServerboundSetCommandMinecartPacket", "ServerboundSetCreativeModeSlotPacket", "ServerboundSetJigsawBlockPacket", "ServerboundSetStructureBlockPacket",
            "ServerboundSignUpdatePacket", "ServerboundStatusRequestPacket", "ServerboundSwingPacket", "ServerboundTeleportToEntityPacket", "ServerboundUseItemOnPacket", "ServerboundUseItemPacket",
            /* [LC-2] 内嵌包变体(追加块,与 packets.c 目录尾部同序):抽象包类
               ServerboundMovePlayerPacket/ClientboundMoveEntityPacket 的
               Packet 子类内嵌变体(1.20.1 全量 javap 核实,见 task-2-report.md);
               混淆名 zx$a-d / wl$a-c,运行时 Class.getName() 返回 a$b 形式,
               PacketMap 键即此形式。 */
            "ClientboundMoveEntityPacket$Pos", "ClientboundMoveEntityPacket$PosRot", "ClientboundMoveEntityPacket$Rot",
            "ServerboundMovePlayerPacket$Pos", "ServerboundMovePlayerPacket$PosRot", "ServerboundMovePlayerPacket$Rot", "ServerboundMovePlayerPacket$StatusOnly",
    };

    /* ---------- 7.1:包名 → 目录 id(组基址 + 组内名字序秩) ----------
     *  id 语义(include/mosaic/packets.h):组基址 + 组内序(组内按名字升序,
     *  与 packets.c 目录序一致)。组分配 = 1.20.1 锚定:登录/状态/握手例外名
     *  (13 条)按表,其余 Serverbound* → PLAY_IN、Clientbound* → PLAY_OUT
     *  (ClientIntentionPacket 无 Serverbound 前缀,1.20.1 特有,单列)。
     *  id 可完全由 PACKET_NAMES + 组表推导——PACKET_NAMES ↔ packets.c 的
     *  N2 双向比对门禁(ApiContractTest)传递保证 id 不漂移;168 条逐项脚本
     *  比对验证记录见 task-7-report.md,[LC-2] 内嵌变体补全(175 条)记录见
     *  task-2-report.md。 */

    private static final java.util.Set<String> LOGIN_IN = java.util.Set.of(
            "ServerboundCustomQueryPacket", "ServerboundHelloPacket",
            /* 前瞻(1.20.2+):ServerboundLoginAcknowledgedPacket 为 1.20.2 新增
               (1.20.2 登录序列拆分),不在 1.20.1 目录/服务端——保留占位以便
               未来版本,1.20.1 环境下不会匹配任何实际包。 */
            "ServerboundLoginAcknowledgedPacket", "ServerboundKeyPacket");
    private static final java.util.Set<String> LOGIN_OUT = java.util.Set.of(
            "ClientboundCustomQueryPacket", "ClientboundGameProfilePacket",
            "ClientboundHelloPacket", "ClientboundLoginCompressionPacket",
            "ClientboundLoginDisconnectPacket");
    private static final java.util.Set<String> STATUS_IN = java.util.Set.of(
            "ServerboundPingRequestPacket", "ServerboundStatusRequestPacket");
    private static final java.util.Set<String> STATUS_OUT = java.util.Set.of(
            "ClientboundPongResponsePacket", "ClientboundStatusResponsePacket");

    /** 包名 → 目录分组(1.20.1 锚定;未知名/无法判定 → null)。 */
    private static String groupOf(String name) {
        if (name == null) return null;
        if ("ClientIntentionPacket".equals(name)) return "HANDSHAKE_IN";
        if (LOGIN_IN.contains(name)) return "LOGIN_IN";
        if (LOGIN_OUT.contains(name)) return "LOGIN_OUT";
        if (STATUS_IN.contains(name)) return "STATUS_IN";
        if (STATUS_OUT.contains(name)) return "STATUS_OUT";
        if (name.startsWith("Serverbound")) return "PLAY_IN";
        if (name.startsWith("Clientbound")) return "PLAY_OUT";
        return null;
    }

    /** 包名 → 目录 id(7.1;Provider packetOf 投影用)。未命中目录 → 0(UNKNOWN)。
     *  id = 组基址 + 组内名字序(与 packets.c 完全一致,见类注释)。 */
    public static int packetIdOf(String name) {
        String g = groupOf(name);
        if (g == null) return 0;
        int base;
        switch (g) {
            case "PLAY_IN": base = 0x0100; break;
            case "PLAY_OUT": base = 0x0200; break;
            case "LOGIN_IN": base = 0x0500; break;
            case "LOGIN_OUT": base = 0x0600; break;
            case "STATUS_IN": base = 0x0700; break;
            case "STATUS_OUT": base = 0x0800; break;
            default: base = 0x0900; break;   // HANDSHAKE_IN
        }
        int rank = 0;
        for (String n : PACKET_NAMES) {
            if (n.equals(name)) return base + 1 + rank;
            if (g.equals(groupOf(n))) rank++;
        }
        return 0;   // 目录外(如 26.2/1.8.9 独有包;PACKET_NAMES 即目录全集)
    }
}
