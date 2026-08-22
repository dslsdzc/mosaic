package mosaic.runtime;

public interface MosaicServiceRegistry {
    /** 任意类型服务均可注册(Task 5 契约测试 register(Runnable.class) 要求;
     *  无界泛型与原 MosaicService 限定兼容——所有既有调用点仍合法)。 */
    <T> void register(Class<T> type, T service);
    <T> T get(Class<T> type);
    /** 可选获取(不存在返回 null)。 */
    <T> T optional(Class<T> type);
}
