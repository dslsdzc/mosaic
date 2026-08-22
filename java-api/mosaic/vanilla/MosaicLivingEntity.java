package mosaic.vanilla;

/** 活体实体细分(26.2 world.entity.LivingEntity ↔ 1.8.9 entity.EntityLivingBase)。 */
public interface MosaicLivingEntity {
    /** 当前生命值。 */
    float health();
    /** 最大生命值。 */
    float maxHealth();
    /** 死亡状态(26.2 isDeadOrDying ↔ 1.8.9 Entity.isDead 字段)。 */
    boolean dead();
}
