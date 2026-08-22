package mosaic.vanilla;

/** 命令:注册/执行(1.8.9 command.* ↔ 26.2 Brigadier)。 */
public interface MosaicCommand {
    /** 注册命令(名 + 执行回调);重名抛 MosaicApiException。 */
    void register(String name, MosaicCommandHandler handler);
}
