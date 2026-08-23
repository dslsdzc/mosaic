package mosaic.runtime.internal;

/** 包目录常量表(网络域,Task 6):packets.c 名字抄录——N2 包目录双端一致门禁
 *  基准(契约测试经 Native.packetCatalogName 探测总数与 PACKET_NAMES 逐项
 *  双向比对,防 packets.c 与 Java 侧漂移;与 EventImpl.EVENT_NAMES 同款纪律,
 *  数量派生自 C 目录探测,不硬编码)。
 *  目录按名字升序,与 packets.c 完全同序;id 分组语义见 include/mosaic/packets.h
 *  (UNKNOWN=0、PLAY_IN 0x0101..、PLAY_OUT 0x0201..、LOGIN_IN 0x0501..、
 *  LOGIN_OUT 0x0601..、STATUS_IN 0x0701..、STATUS_OUT 0x0801..、
 *  HANDSHAKE_IN 0x0901..;CONFIG 组 1.20.1 为空)。
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
    };
}
