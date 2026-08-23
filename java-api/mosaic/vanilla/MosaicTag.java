package mosaic.vanilla;

/** 标签域(26.2 tags.TagKey ↔ 1.8.9 无标签系统,Provider null 语义吸收)。
 *  逆向核实:标签注册表随 1.13 数据驱动注册表引入,1.8.9 jar 无 net.minecraft.tags 包。 */
public interface MosaicTag {
    /** 标签注册名(如 "minecraft:planks")。 */
    String registryName();
    /** 包含的注册表条目名(26.2 已绑定标签 → 真实内容:注册表 getTagOrEmpty →
     *  Holder.value → getKey;数据驱动,契约环境未绑定 → 空;1.8.9 无标签系统 →
     *  恒空降级,与 26.2 不对称)。 */
    String[] contents();
}
