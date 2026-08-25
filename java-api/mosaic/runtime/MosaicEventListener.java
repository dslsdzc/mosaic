package mosaic.runtime;

import mosaic.Since;

/**
 * 事件监听器(Java 观测通道):事件派发返回后广播。
 *
 * 与 {@link MosaicEventHandler}(订阅)的区别:
 * <ul>
 *   <li>handler 是派发路径的一部分——其执行计入派发返回值,异常向上传播
 *       (既有语义,本类型不改动);</li>
 *   <li>listener 是观测者——在 C 内核派发与 Java handler 执行完毕后另行
 *       通知,单个监听器异常被隔离(不影响其余监听器与派发返回值)。</li>
 * </ul>
 *
 * 广播语义(EventImpl.dispatch):
 * <ol>
 *   <li>先执行 C 内核订阅者(Native.eventDispatch)+ Java handler(既有
 *       顺序,返回/计数不变);</li>
 *   <li>派发返回后,向该事件的监听器广播:事件目录条目、C 内核执行数、
 *       原始载荷 byte[];</li>
 *   <li>重入保护:监听器回调内再派发 → 同线程广播深度超过上限(8)时
 *       丢弃该次广播(嵌套派发本身照常执行),防无限循环;</li>
 *   <li>异常隔离:单个监听器抛异常 → 打印告警并继续其余监听器。</li>
 * </ol>
 *
 * 载荷为派发原引用(只读约定:修改会影响同一广播内后续监听器;C 内核
 * 侧为 JNI_ABORT 只读读入,不保留引用)。
 */
@Since(1)
@FunctionalInterface
public interface MosaicEventListener {
    /**
     * 派发返回后回调。
     *
     * @param event    按 eventId 解析的事件目录条目(未注册事件 → null)
     * @param executed 本次派发的 C 内核订阅者执行数(EventImpl.dispatch
     *                 返回值的 C 部分,不含 Java handler 计数)
     * @param payload  派发载荷原样(只读约定,见类注释)
     */
    void onEventDispatched(MosaicEvent event, int executed, byte[] payload);
}
