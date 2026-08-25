/* src/packets.c — M6-E:包类型目录 v1(网络域;名字 + 方向分组 id)。
   原始 168 条按名字升序(ASCII 名:strcmp 序与 events.c 长度感知序一致);
   id 按分组连续分配(UNKNOWN=0;组基址见 packets.h 注释,组内顺序 = 目录序)。
   [LC-2] 内嵌包变体 7 条追加在目录尾部(追加块内按名升序;既有 168 条 id
   不动,新 id 按组续号)——变体名字含 '$',strcmp 序位在原 168 条之前,故
   只能作追加块(详见 packets.h 注释)。
   清单来源:Mojang server_mappings(server.txt,1.20.1,2026-08-23 下载,
   sha1 0b4dba049482496c507b2387a73a913230ebbd76)提取的
   net.minecraft.network.protocol.* 下全部 Serverbound 与 Clientbound 包类
   + 内嵌包变体(175 类 = 168 顶层 + 7 变体;协议态分布:PLAY_IN 50 /
   PLAY_OUT 112 / LOGIN_IN 3 / LOGIN_OUT 5 / STATUS_IN 2 / STATUS_OUT 2 /
   HANDSHAKE_IN 1;CONFIG_IN/OUT 1.20.1 无 config 态为空)。混淆名配对由
   ci/gen_packet_map.sh 校验并生成 agent 映射表(5+ 条 javap 实测抽查记录
   见 task-6-report.md;变体核实记录见 task-2-report.md)。
   BundleDelimiterPacket/BundlePacket(协议根包,编码器内非方向性工具类)
   不入目录——出现即 UNKNOWN(0)。 */
#include "mosaic/packets.h"
#include <string.h>

const mosaic_packet_entry mosaic_packets_catalog[] = {
  { "ClientIntentionPacket", 0x0901 },   /* HANDSHAKE_IN */
  { "ClientboundAddEntityPacket", 0x0201 },   /* PLAY_OUT */
  { "ClientboundAddExperienceOrbPacket", 0x0202 },   /* PLAY_OUT */
  { "ClientboundAddPlayerPacket", 0x0203 },   /* PLAY_OUT */
  { "ClientboundAnimatePacket", 0x0204 },   /* PLAY_OUT */
  { "ClientboundAwardStatsPacket", 0x0205 },   /* PLAY_OUT */
  { "ClientboundBlockChangedAckPacket", 0x0206 },   /* PLAY_OUT */
  { "ClientboundBlockDestructionPacket", 0x0207 },   /* PLAY_OUT */
  { "ClientboundBlockEntityDataPacket", 0x0208 },   /* PLAY_OUT */
  { "ClientboundBlockEventPacket", 0x0209 },   /* PLAY_OUT */
  { "ClientboundBlockUpdatePacket", 0x020A },   /* PLAY_OUT */
  { "ClientboundBossEventPacket", 0x020B },   /* PLAY_OUT */
  { "ClientboundBundlePacket", 0x020C },   /* PLAY_OUT */
  { "ClientboundChangeDifficultyPacket", 0x020D },   /* PLAY_OUT */
  { "ClientboundChunksBiomesPacket", 0x020E },   /* PLAY_OUT */
  { "ClientboundClearTitlesPacket", 0x020F },   /* PLAY_OUT */
  { "ClientboundCommandSuggestionsPacket", 0x0210 },   /* PLAY_OUT */
  { "ClientboundCommandsPacket", 0x0211 },   /* PLAY_OUT */
  { "ClientboundContainerClosePacket", 0x0212 },   /* PLAY_OUT */
  { "ClientboundContainerSetContentPacket", 0x0213 },   /* PLAY_OUT */
  { "ClientboundContainerSetDataPacket", 0x0214 },   /* PLAY_OUT */
  { "ClientboundContainerSetSlotPacket", 0x0215 },   /* PLAY_OUT */
  { "ClientboundCooldownPacket", 0x0216 },   /* PLAY_OUT */
  { "ClientboundCustomChatCompletionsPacket", 0x0217 },   /* PLAY_OUT */
  { "ClientboundCustomPayloadPacket", 0x0218 },   /* PLAY_OUT */
  { "ClientboundCustomQueryPacket", 0x0601 },   /* LOGIN_OUT */
  { "ClientboundDamageEventPacket", 0x0219 },   /* PLAY_OUT */
  { "ClientboundDeleteChatPacket", 0x021A },   /* PLAY_OUT */
  { "ClientboundDisconnectPacket", 0x021B },   /* PLAY_OUT */
  { "ClientboundDisguisedChatPacket", 0x021C },   /* PLAY_OUT */
  { "ClientboundEntityEventPacket", 0x021D },   /* PLAY_OUT */
  { "ClientboundExplodePacket", 0x021E },   /* PLAY_OUT */
  { "ClientboundForgetLevelChunkPacket", 0x021F },   /* PLAY_OUT */
  { "ClientboundGameEventPacket", 0x0220 },   /* PLAY_OUT */
  { "ClientboundGameProfilePacket", 0x0602 },   /* LOGIN_OUT */
  { "ClientboundHelloPacket", 0x0603 },   /* LOGIN_OUT */
  { "ClientboundHorseScreenOpenPacket", 0x0221 },   /* PLAY_OUT */
  { "ClientboundHurtAnimationPacket", 0x0222 },   /* PLAY_OUT */
  { "ClientboundInitializeBorderPacket", 0x0223 },   /* PLAY_OUT */
  { "ClientboundKeepAlivePacket", 0x0224 },   /* PLAY_OUT */
  { "ClientboundLevelChunkWithLightPacket", 0x0225 },   /* PLAY_OUT */
  { "ClientboundLevelEventPacket", 0x0226 },   /* PLAY_OUT */
  { "ClientboundLevelParticlesPacket", 0x0227 },   /* PLAY_OUT */
  { "ClientboundLightUpdatePacket", 0x0228 },   /* PLAY_OUT */
  { "ClientboundLoginCompressionPacket", 0x0604 },   /* LOGIN_OUT */
  { "ClientboundLoginDisconnectPacket", 0x0605 },   /* LOGIN_OUT */
  { "ClientboundLoginPacket", 0x0229 },   /* PLAY_OUT */
  { "ClientboundMapItemDataPacket", 0x022A },   /* PLAY_OUT */
  { "ClientboundMerchantOffersPacket", 0x022B },   /* PLAY_OUT */
  { "ClientboundMoveEntityPacket", 0x022C },   /* PLAY_OUT */
  { "ClientboundMoveVehiclePacket", 0x022D },   /* PLAY_OUT */
  { "ClientboundOpenBookPacket", 0x022E },   /* PLAY_OUT */
  { "ClientboundOpenScreenPacket", 0x022F },   /* PLAY_OUT */
  { "ClientboundOpenSignEditorPacket", 0x0230 },   /* PLAY_OUT */
  { "ClientboundPingPacket", 0x0231 },   /* PLAY_OUT */
  { "ClientboundPlaceGhostRecipePacket", 0x0232 },   /* PLAY_OUT */
  { "ClientboundPlayerAbilitiesPacket", 0x0233 },   /* PLAY_OUT */
  { "ClientboundPlayerChatPacket", 0x0234 },   /* PLAY_OUT */
  { "ClientboundPlayerCombatEndPacket", 0x0235 },   /* PLAY_OUT */
  { "ClientboundPlayerCombatEnterPacket", 0x0236 },   /* PLAY_OUT */
  { "ClientboundPlayerCombatKillPacket", 0x0237 },   /* PLAY_OUT */
  { "ClientboundPlayerInfoRemovePacket", 0x0238 },   /* PLAY_OUT */
  { "ClientboundPlayerInfoUpdatePacket", 0x0239 },   /* PLAY_OUT */
  { "ClientboundPlayerLookAtPacket", 0x023A },   /* PLAY_OUT */
  { "ClientboundPlayerPositionPacket", 0x023B },   /* PLAY_OUT */
  { "ClientboundPongResponsePacket", 0x0801 },   /* STATUS_OUT */
  { "ClientboundRecipePacket", 0x023C },   /* PLAY_OUT */
  { "ClientboundRemoveEntitiesPacket", 0x023D },   /* PLAY_OUT */
  { "ClientboundRemoveMobEffectPacket", 0x023E },   /* PLAY_OUT */
  { "ClientboundResourcePackPacket", 0x023F },   /* PLAY_OUT */
  { "ClientboundRespawnPacket", 0x0240 },   /* PLAY_OUT */
  { "ClientboundRotateHeadPacket", 0x0241 },   /* PLAY_OUT */
  { "ClientboundSectionBlocksUpdatePacket", 0x0242 },   /* PLAY_OUT */
  { "ClientboundSelectAdvancementsTabPacket", 0x0243 },   /* PLAY_OUT */
  { "ClientboundServerDataPacket", 0x0244 },   /* PLAY_OUT */
  { "ClientboundSetActionBarTextPacket", 0x0245 },   /* PLAY_OUT */
  { "ClientboundSetBorderCenterPacket", 0x0246 },   /* PLAY_OUT */
  { "ClientboundSetBorderLerpSizePacket", 0x0247 },   /* PLAY_OUT */
  { "ClientboundSetBorderSizePacket", 0x0248 },   /* PLAY_OUT */
  { "ClientboundSetBorderWarningDelayPacket", 0x0249 },   /* PLAY_OUT */
  { "ClientboundSetBorderWarningDistancePacket", 0x024A },   /* PLAY_OUT */
  { "ClientboundSetCameraPacket", 0x024B },   /* PLAY_OUT */
  { "ClientboundSetCarriedItemPacket", 0x024C },   /* PLAY_OUT */
  { "ClientboundSetChunkCacheCenterPacket", 0x024D },   /* PLAY_OUT */
  { "ClientboundSetChunkCacheRadiusPacket", 0x024E },   /* PLAY_OUT */
  { "ClientboundSetDefaultSpawnPositionPacket", 0x024F },   /* PLAY_OUT */
  { "ClientboundSetDisplayObjectivePacket", 0x0250 },   /* PLAY_OUT */
  { "ClientboundSetEntityDataPacket", 0x0251 },   /* PLAY_OUT */
  { "ClientboundSetEntityLinkPacket", 0x0252 },   /* PLAY_OUT */
  { "ClientboundSetEntityMotionPacket", 0x0253 },   /* PLAY_OUT */
  { "ClientboundSetEquipmentPacket", 0x0254 },   /* PLAY_OUT */
  { "ClientboundSetExperiencePacket", 0x0255 },   /* PLAY_OUT */
  { "ClientboundSetHealthPacket", 0x0256 },   /* PLAY_OUT */
  { "ClientboundSetObjectivePacket", 0x0257 },   /* PLAY_OUT */
  { "ClientboundSetPassengersPacket", 0x0258 },   /* PLAY_OUT */
  { "ClientboundSetPlayerTeamPacket", 0x0259 },   /* PLAY_OUT */
  { "ClientboundSetScorePacket", 0x025A },   /* PLAY_OUT */
  { "ClientboundSetSimulationDistancePacket", 0x025B },   /* PLAY_OUT */
  { "ClientboundSetSubtitleTextPacket", 0x025C },   /* PLAY_OUT */
  { "ClientboundSetTimePacket", 0x025D },   /* PLAY_OUT */
  { "ClientboundSetTitleTextPacket", 0x025E },   /* PLAY_OUT */
  { "ClientboundSetTitlesAnimationPacket", 0x025F },   /* PLAY_OUT */
  { "ClientboundSoundEntityPacket", 0x0260 },   /* PLAY_OUT */
  { "ClientboundSoundPacket", 0x0261 },   /* PLAY_OUT */
  { "ClientboundStatusResponsePacket", 0x0802 },   /* STATUS_OUT */
  { "ClientboundStopSoundPacket", 0x0262 },   /* PLAY_OUT */
  { "ClientboundSystemChatPacket", 0x0263 },   /* PLAY_OUT */
  { "ClientboundTabListPacket", 0x0264 },   /* PLAY_OUT */
  { "ClientboundTagQueryPacket", 0x0265 },   /* PLAY_OUT */
  { "ClientboundTakeItemEntityPacket", 0x0266 },   /* PLAY_OUT */
  { "ClientboundTeleportEntityPacket", 0x0267 },   /* PLAY_OUT */
  { "ClientboundUpdateAdvancementsPacket", 0x0268 },   /* PLAY_OUT */
  { "ClientboundUpdateAttributesPacket", 0x0269 },   /* PLAY_OUT */
  { "ClientboundUpdateEnabledFeaturesPacket", 0x026A },   /* PLAY_OUT */
  { "ClientboundUpdateMobEffectPacket", 0x026B },   /* PLAY_OUT */
  { "ClientboundUpdateRecipesPacket", 0x026C },   /* PLAY_OUT */
  { "ClientboundUpdateTagsPacket", 0x026D },   /* PLAY_OUT */
  { "ServerboundAcceptTeleportationPacket", 0x0101 },   /* PLAY_IN */
  { "ServerboundChangeDifficultyPacket", 0x0102 },   /* PLAY_IN */
  { "ServerboundChatAckPacket", 0x0103 },   /* PLAY_IN */
  { "ServerboundChatCommandPacket", 0x0104 },   /* PLAY_IN */
  { "ServerboundChatPacket", 0x0105 },   /* PLAY_IN */
  { "ServerboundChatSessionUpdatePacket", 0x0106 },   /* PLAY_IN */
  { "ServerboundClientCommandPacket", 0x0107 },   /* PLAY_IN */
  { "ServerboundClientInformationPacket", 0x0108 },   /* PLAY_IN */
  { "ServerboundCommandSuggestionPacket", 0x0109 },   /* PLAY_IN */
  { "ServerboundContainerButtonClickPacket", 0x010A },   /* PLAY_IN */
  { "ServerboundContainerClickPacket", 0x010B },   /* PLAY_IN */
  { "ServerboundContainerClosePacket", 0x010C },   /* PLAY_IN */
  { "ServerboundCustomPayloadPacket", 0x010D },   /* PLAY_IN */
  { "ServerboundCustomQueryPacket", 0x0501 },   /* LOGIN_IN */
  { "ServerboundEditBookPacket", 0x010E },   /* PLAY_IN */
  { "ServerboundHelloPacket", 0x0502 },   /* LOGIN_IN */
  { "ServerboundInteractPacket", 0x010F },   /* PLAY_IN */
  { "ServerboundJigsawGeneratePacket", 0x0110 },   /* PLAY_IN */
  { "ServerboundKeepAlivePacket", 0x0111 },   /* PLAY_IN */
  { "ServerboundKeyPacket", 0x0503 },   /* LOGIN_IN */
  { "ServerboundLockDifficultyPacket", 0x0112 },   /* PLAY_IN */
  { "ServerboundMovePlayerPacket", 0x0113 },   /* PLAY_IN */
  { "ServerboundMoveVehiclePacket", 0x0114 },   /* PLAY_IN */
  { "ServerboundPaddleBoatPacket", 0x0115 },   /* PLAY_IN */
  { "ServerboundPickItemPacket", 0x0116 },   /* PLAY_IN */
  { "ServerboundPingRequestPacket", 0x0701 },   /* STATUS_IN */
  { "ServerboundPlaceRecipePacket", 0x0117 },   /* PLAY_IN */
  { "ServerboundPlayerAbilitiesPacket", 0x0118 },   /* PLAY_IN */
  { "ServerboundPlayerActionPacket", 0x0119 },   /* PLAY_IN */
  { "ServerboundPlayerCommandPacket", 0x011A },   /* PLAY_IN */
  { "ServerboundPlayerInputPacket", 0x011B },   /* PLAY_IN */
  { "ServerboundPongPacket", 0x011C },   /* PLAY_IN */
  { "ServerboundRecipeBookChangeSettingsPacket", 0x011D },   /* PLAY_IN */
  { "ServerboundRecipeBookSeenRecipePacket", 0x011E },   /* PLAY_IN */
  { "ServerboundRenameItemPacket", 0x011F },   /* PLAY_IN */
  { "ServerboundResourcePackPacket", 0x0120 },   /* PLAY_IN */
  { "ServerboundSeenAdvancementsPacket", 0x0121 },   /* PLAY_IN */
  { "ServerboundSelectTradePacket", 0x0122 },   /* PLAY_IN */
  { "ServerboundSetBeaconPacket", 0x0123 },   /* PLAY_IN */
  { "ServerboundSetCarriedItemPacket", 0x0124 },   /* PLAY_IN */
  { "ServerboundSetCommandBlockPacket", 0x0125 },   /* PLAY_IN */
  { "ServerboundSetCommandMinecartPacket", 0x0126 },   /* PLAY_IN */
  { "ServerboundSetCreativeModeSlotPacket", 0x0127 },   /* PLAY_IN */
  { "ServerboundSetJigsawBlockPacket", 0x0128 },   /* PLAY_IN */
  { "ServerboundSetStructureBlockPacket", 0x0129 },   /* PLAY_IN */
  { "ServerboundSignUpdatePacket", 0x012A },   /* PLAY_IN */
  { "ServerboundStatusRequestPacket", 0x0702 },   /* STATUS_IN */
  { "ServerboundSwingPacket", 0x012B },   /* PLAY_IN */
  { "ServerboundTeleportToEntityPacket", 0x012C },   /* PLAY_IN */
  { "ServerboundUseItemOnPacket", 0x012D },   /* PLAY_IN */
  { "ServerboundUseItemPacket", 0x012E },   /* PLAY_IN */
  /* [LC-2] 内嵌包变体追加块:ServerboundMovePlayerPacket/ClientboundMoveEntity
     Packet 为抽象包类,运行时实体 = 内嵌子类(混淆名 zx$a-d / wl$a-c,
     getClass().getName() 返回 a$b 形式——映射表键即此形式,挂钩查表无需
     改动)。此前查表未命中 → packet_id=0(UNKNOWN);现映射到本组续号 id
     (PLAY_IN 续 0x012F..、PLAY_OUT 续 0x026E..)。混淆名 + Packet 子类性
     经 server_mappings + javap 全量实测核实(javap 输出摘录见
     .superpowers/sdd/task-2-report.md);其余包类的内嵌类(枚举/接口/记录/
     数据持有类)非 Packet 子类,不入目录。块内按名升序。 */
  { "ClientboundMoveEntityPacket$Pos", 0x026E },   /* PLAY_OUT */
  { "ClientboundMoveEntityPacket$PosRot", 0x026F },   /* PLAY_OUT */
  { "ClientboundMoveEntityPacket$Rot", 0x0270 },   /* PLAY_OUT */
  { "ServerboundMovePlayerPacket$Pos", 0x012F },   /* PLAY_IN */
  { "ServerboundMovePlayerPacket$PosRot", 0x0130 },   /* PLAY_IN */
  { "ServerboundMovePlayerPacket$Rot", 0x0131 },   /* PLAY_IN */
  { "ServerboundMovePlayerPacket$StatusOnly", 0x0132 },   /* PLAY_IN */
};
const u32 mosaic_packets_catalog_count =
    (u32)(sizeof(mosaic_packets_catalog) / sizeof(mosaic_packets_catalog[0]));

/* M6-E N2:目录访问器(跨语言一致性门禁;实现见 packets.h 声明注释)。 */
const char *mosaic_packet_catalog_name(u32 index) {
  if (index >= mosaic_packets_catalog_count) return NULL;
  return mosaic_packets_catalog[index].name;
}

