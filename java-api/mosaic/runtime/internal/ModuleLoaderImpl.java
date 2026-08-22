package mosaic.runtime.internal;

import mosaic.MosaicHandleException;
import mosaic.runtime.MosaicModule;
import mosaic.runtime.MosaicModuleContext;
import mosaic.runtime.MosaicModuleLoader;
import mosaic.runtime.MosaicResourceHandle;

/** 模块装载实现:load/unload 直通 mod_load/mod_unload;模块元数据经模块
 *  描述符(modDescField/modDescString)读取。
 *
 *  ⚠️ 8 文档注记(Task 5):moduleCount/moduleDescriptor 是基础 pack 视图
 *  (find_module_ex,仅扫描 rt->packs 基础 pack 头/记录);而 mod_load 走
 *  活跃视图(find_module_active,已 commit 补丁的模块记录优先)。补丁
 *  commit 后两视图的版本/so_path 可能不一致——索引/描述符返回基础 pack
 *  元数据,实际 dlopen 的 .so 以补丁为准。 */
public final class ModuleLoaderImpl implements MosaicModuleLoader {
    private final RuntimeImpl rt;

    ModuleLoaderImpl(RuntimeImpl rt) { this.rt = rt; }

    public MosaicModule load(long moduleId) {
        long abi = Native.moduleLoad(rt.handle(), moduleId);
        if (abi == 0)
            throw new MosaicHandleException("module load failed (lastError=" + Native.lastError(rt.handle()) + ")");
        long d = Native.moduleDescriptor(rt.handle(), moduleId);
        long h = rt.handle();
        long mid = d != 0 ? Native.modDescField(h, d, 0) : moduleId;
        int version = d != 0 ? (int) Native.modDescField(h, d, 1) : 0;
        int generation = d != 0 ? (int) Native.modDescField(h, d, 2) : 0;
        String name = d != 0 ? Native.modDescString(h, d, 0) : null;
        return new ModuleImpl(rt, mid, version, generation, name);
    }

    public void unload(long moduleId) { Native.moduleUnload(rt.handle(), moduleId); }

    static final class ModuleImpl implements MosaicModule {
        private final RuntimeImpl rt;
        private final long moduleId;
        private final int version, generation;
        private final String name;

        ModuleImpl(RuntimeImpl rt, long moduleId, int version, int generation, String name) {
            this.rt = rt; this.moduleId = moduleId;
            this.version = version; this.generation = generation; this.name = name;
        }
        public long moduleId() { return moduleId; }
        public int version() { return version; }
        public int generation() { return generation; }
        public String name() { return name; }
        public MosaicModuleContext context() { return new ModuleContextImpl(rt, moduleId); }
    }

    static final class ModuleContextImpl implements MosaicModuleContext {
        private final RuntimeImpl rt;
        private final long moduleId;
        ModuleContextImpl(RuntimeImpl rt, long moduleId) {
            this.rt = rt; this.moduleId = moduleId;
        }
        public void unload() { Native.moduleUnload(rt.handle(), moduleId); }
        public MosaicResourceHandle resource() { return new ResourceHandleImpl(); }
    }

    static final class ResourceHandleImpl implements MosaicResourceHandle {
        private boolean valid = true;
        public boolean valid() { return valid; }
        public void invalidate() { valid = false; }
    }
}
