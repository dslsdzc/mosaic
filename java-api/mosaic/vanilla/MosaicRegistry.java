package mosaic.vanilla;

/** 注册表:id↔名双向映射(数字 id/注册表 id 差异全在 Provider)。 */
public interface MosaicRegistry {
    /** 名 → id;未注册 -1。 */
    int id(String registryName);
    /** id → 名;未注册 null。 */
    String name(int id);
}
