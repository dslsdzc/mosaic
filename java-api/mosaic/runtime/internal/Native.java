package mosaic.runtime.internal;

/** JNI 直通:静态委托到 mosaic.Bridge 的 native 方法(JNI 符号统一 Java_mosaic_Bridge_*,
   避免 Native 包名下产生第二组符号;加载由 Bridge 静态块完成)。 */
public final class Native {
    private Native() {}
    public static long runtimeOpen(String[] paths) { return mosaic.Bridge.runtimeOpen(paths); }
    public static void runtimeClose(long rt) { mosaic.Bridge.runtimeClose(rt); }
    public static long functionCount(long rt) { return mosaic.Bridge.functionCount(rt); }
    public static int eventId(long rt, String name) { return mosaic.Bridge.eventId(rt, name); }
    public static int eventDispatch(long rt, int eventId, byte[] payload) {
        return mosaic.Bridge.eventDispatch(rt, eventId, payload); }
    public static int workingSetCount(long rt) { return mosaic.Bridge.workingSetCount(rt); }
    public static int lastError(long rt) { return mosaic.Bridge.lastError(rt); }
    public static int runtimeAddPack(long rt, String path) { return mosaic.Bridge.runtimeAddPack(rt, path); }
    public static int packCount(long rt) { return mosaic.Bridge.packCount(rt); }

    public static long fnMaterialize(long rt, long fnId) { return mosaic.Bridge.fnMaterialize(rt, fnId); }
    public static int fnTombstone(long rt, long fnHandle) { return mosaic.Bridge.fnTombstone(rt, fnHandle); }
    public static void fnExecute(long rt, long fnHandle, int eventId, byte[] payload) {
        mosaic.Bridge.fnExecute(rt, fnHandle, eventId, payload); }
    public static byte[] fnState(long rt, long fnHandle) { return mosaic.Bridge.fnState(rt, fnHandle); }
    public static long fnIdOf(long rt, long fnHandle) { return mosaic.Bridge.fnIdOf(rt, fnHandle); }
    /* M6-B:状态写 / 触发订阅者列出(一行委托) */
    public static int fnStateWrite(long rt, long fnHandle, byte[] state) {
        return mosaic.Bridge.fnStateWrite(rt, fnHandle, state); }
    public static int triggerSubscribers(long rt, int eventId, long[] out) {
        return mosaic.Bridge.triggerSubscribers(rt, eventId, out); }

    public static long packCreate(String path, long mc, long fc, long tc, long dc, int ec) {
        return mosaic.Bridge.packCreate(path, mc, fc, tc, dc, ec); }
    public static void packAddEvent(long b, String name) { mosaic.Bridge.packAddEvent(b, name); }
    public static void packAddModule(long b, long mid, int ver, String name, String so) {
        mosaic.Bridge.packAddModule(b, mid, ver, name, so); }
    public static void packAddFn(long b, long mid, long local, int codeOff, int stateSize,
                                 int gen, int cost, int flags) {
        mosaic.Bridge.packAddFn(b, mid, local, codeOff, stateSize, gen, cost, flags); }
    public static void packSetFnTransform(long b, long fnId, int idx) {
        mosaic.Bridge.packSetFnTransform(b, fnId, idx); }
    public static void packAddTrigger(long b, int eventId, long fnId) {
        mosaic.Bridge.packAddTrigger(b, eventId, fnId); }
    public static void packAddDep(long b, long owner, long dep) { mosaic.Bridge.packAddDep(b, owner, dep); }
    public static void packSetItemCount(long b, long n) { mosaic.Bridge.packSetItemCount(b, n); }
    public static void packAddItem(long b, long provider, String name, String tags,
                                   int category, String icon, int flags) {
        mosaic.Bridge.packAddItem(b, provider, name, tags, category, icon, flags); }
    public static int packFinish(long b) { return mosaic.Bridge.packFinish(b); }
    public static void packFree(long b) { mosaic.Bridge.packFree(b); }

    public static long fnDescriptor(long rt, long fnId) { return mosaic.Bridge.fnDescriptor(rt, fnId); }
    public static long fnDescField(long rt, long desc, int field) {
        return mosaic.Bridge.fnDescField(rt, desc, field); }
    public static long moduleDescriptor(long rt, long moduleId) {
        return mosaic.Bridge.moduleDescriptor(rt, moduleId); }
    public static long modDescField(long rt, long desc, int field) {
        return mosaic.Bridge.modDescField(rt, desc, field); }
    public static String modDescString(long rt, long desc, int field) {
        return mosaic.Bridge.modDescString(rt, desc, field); }

    /* M5-2(Task 4)追加:item 描述符 / 枚举 / 模块装载 / 依赖 / 驱逐 / 租约 / 事务
       ——全部一行委托到 mosaic.Bridge 对应 native */
    public static long itemCount(long rt) { return mosaic.Bridge.itemCount(rt); }
    public static long itemDescriptor(long rt, int category, String name) {
        return mosaic.Bridge.itemDescriptor(rt, category, name); }
    public static long itemDescField(long rt, long desc, int field) {
        return mosaic.Bridge.itemDescField(rt, desc, field); }
    public static String itemDescString(long rt, long desc, int field) {
        return mosaic.Bridge.itemDescString(rt, desc, field); }
    public static long[] itemForEachCategory(long rt, int category) {
        return mosaic.Bridge.itemForEachCategory(rt, category); }
    public static long moduleLoad(long rt, long moduleId) { return mosaic.Bridge.moduleLoad(rt, moduleId); }
    public static void moduleUnload(long rt, long moduleId) { mosaic.Bridge.moduleUnload(rt, moduleId); }
    public static long moduleCount(long rt) { return mosaic.Bridge.moduleCount(rt); }
    public static int depResolve(long rt, long moduleId, int minVer, int maxVer, long[] out) {
        return mosaic.Bridge.depResolve(rt, moduleId, minVer, maxVer, out); }
    public static int evictIdle(long rt, long windowNanos) { return mosaic.Bridge.evictIdle(rt, windowNanos); }
    public static long[] activeFnIds(long rt) { return mosaic.Bridge.activeFnIds(rt); }
    public static long leaseAcquire(long rt, long fnId) { return mosaic.Bridge.leaseAcquire(rt, fnId); }
    public static void leaseRelease(long lease) { mosaic.Bridge.leaseRelease(lease); }
    public static long txBegin(long rt, String patchPath) { return mosaic.Bridge.txBegin(rt, patchPath); }
    public static int txValidate(long tx) { return mosaic.Bridge.txValidate(tx); }
    public static int txCommit(long tx) { return mosaic.Bridge.txCommit(tx); }
    public static int txRollback(long tx) { return mosaic.Bridge.txRollback(tx); }
    public static void txAbort(long tx) { mosaic.Bridge.txAbort(tx); }
    public static void txFree(long tx) { mosaic.Bridge.txFree(tx); }

    /* M6-C:元数据(一行委托)——直接依赖遍历 / pack 计数 / tx 补丁 fn 表 */
    public static int depForEach(long rt, long moduleId, long[] out) {
        return mosaic.Bridge.depForEach(rt, moduleId, out); }
    public static long packInfoCount(long rt, int field) {
        return mosaic.Bridge.packInfoCount(rt, field); }
    public static int txPatchFnIds(long tx, long[] out) {
        return mosaic.Bridge.txPatchFnIds(tx, out); }
    /* M6-D:事件目录访问器(契约门禁用) */
    public static String eventCatalogName(int index) {
        return mosaic.Bridge.eventCatalogName(index); }
    /* M6-E:包目录访问器(契约门禁用;packets.c ↔ PACKET_NAMES 双向比对) */
    public static String packetCatalogName(int index) {
        return mosaic.Bridge.packetCatalogName(index); }
    /* LC-3:包目录实际 id 访问器(公式门禁用;越界 → 0)。 */
    public static int packetCatalogId(int index) {
        return mosaic.Bridge.packetCatalogId(index); }
}
