package mosaic.vanilla;

/** 实体:稳定句柄。 */
public interface MosaicEntity {
    MosaicEntityId id();
    MosaicEntityType type();
    double x(); double y(); double z();
    /** 属性值(如 "minecraft:max_health");未知属性抛 MosaicHandleException。 */
    double attribute(String name);
}
