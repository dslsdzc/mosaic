package mosaic.vanilla;

/** 状态效果(26.2 world.effect.MobEffectInstance ↔ 1.8.9 potion.PotionEffect)。 */
public interface MosaicStatusEffect {
    /** 注册表名(26.2 "minecraft:regeneration" ↔ 1.8.9 potion 映射,Provider 吸收)。 */
    String registryName();
    /** 效果强度(放大器 0 起)。 */
    int amplifier();
    /** 剩余刻数。 */
    int duration();
}
