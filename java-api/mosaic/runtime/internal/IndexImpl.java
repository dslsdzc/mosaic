package mosaic.runtime.internal;

import java.util.function.Consumer;
import mosaic.runtime.MosaicEventIndex;
import mosaic.runtime.MosaicFunctionDescriptor;
import mosaic.runtime.MosaicFunctionIndex;
import mosaic.runtime.MosaicIndexQuery;
import mosaic.runtime.MosaicItemDescriptor;
import mosaic.runtime.MosaicItemIndex;
import mosaic.runtime.MosaicModuleDescriptor;
import mosaic.runtime.MosaicModuleIndex;

/** 索引查询实现(纯冷态,零物化):fn/mod/item 描述符直通 mmap 访问器。 */
public final class IndexImpl implements MosaicIndexQuery {
    private final RuntimeImpl rt;
    private final FunctionIndexImpl functions = new FunctionIndexImpl();
    private final ModuleIndexImpl modules = new ModuleIndexImpl();
    private final EventIndexImpl events = new EventIndexImpl();
    private final ItemIndexImpl items = new ItemIndexImpl();

    IndexImpl(RuntimeImpl rt) { this.rt = rt; }

    public MosaicFunctionIndex functions() { return functions; }
    public MosaicModuleIndex modules() { return modules; }
    public MosaicEventIndex events() { return events; }
    public MosaicItemIndex items() { return items; }

    /* ---- 函数索引 ---- */
    private final class FunctionIndexImpl implements MosaicFunctionIndex {
        public MosaicFunctionDescriptor find(long fnId) {
            long d = Native.fnDescriptor(rt.handle(), fnId);
            if (d == 0) return null;
            long h = rt.handle();
            return new FunctionDescriptorImpl(
                Native.fnDescField(h, d, 0), Native.fnDescField(h, d, 1),
                (int) Native.fnDescField(h, d, 2), (int) Native.fnDescField(h, d, 3),
                (int) Native.fnDescField(h, d, 4), (int) Native.fnDescField(h, d, 5),
                (int) Native.fnDescField(h, d, 6));
        }
        public long count() { return Native.functionCount(rt.handle()); }
    }

    /** fn 描述符:字段由 fnDescField 读记录直通访问器。 */
    static final class FunctionDescriptorImpl implements MosaicFunctionDescriptor {
        private final long fnId, moduleId;
        private final int codeOffset, generation, stateSize, costHint, flags;
        FunctionDescriptorImpl(long fnId, long moduleId, int codeOffset, int generation,
                               int stateSize, int costHint, int flags) {
            this.fnId = fnId; this.moduleId = moduleId; this.codeOffset = codeOffset;
            this.generation = generation; this.stateSize = stateSize;
            this.costHint = costHint; this.flags = flags;
        }
        public long fnId() { return fnId; }
        public long moduleId() { return moduleId; }
        public int codeOffset() { return codeOffset; }
        public int generation() { return generation; }
        public int stateSize() { return stateSize; }
        public int costHint() { return costHint; }
        public int flags() { return flags; }
    }

    /* ---- 模块索引 ---- */
    private final class ModuleIndexImpl implements MosaicModuleIndex {
        public MosaicModuleDescriptor find(long moduleId) {
            long d = Native.moduleDescriptor(rt.handle(), moduleId);
            if (d == 0) return null;
            long h = rt.handle();
            return new ModuleDescriptorImpl(
                Native.modDescField(h, d, 0), (int) Native.modDescField(h, d, 1),
                (int) Native.modDescField(h, d, 2),
                Native.modDescString(h, d, 0), Native.modDescString(h, d, 1));
        }
        public long count() { return Native.moduleCount(rt.handle()); }
    }

    static final class ModuleDescriptorImpl implements MosaicModuleDescriptor {
        private final long moduleId;
        private final int version, generation;
        private final String name, soPath;
        ModuleDescriptorImpl(long moduleId, int version, int generation, String name, String soPath) {
            this.moduleId = moduleId; this.version = version; this.generation = generation;
            this.name = name; this.soPath = soPath;
        }
        public long moduleId() { return moduleId; }
        public int version() { return version; }
        public int generation() { return generation; }
        public String name() { return name; }
        public String soPath() { return soPath; }
    }

    /* ---- 事件索引 ---- */
    private final class EventIndexImpl implements MosaicEventIndex {
        public int id(String name) { return Native.eventId(rt.handle(), name); }
        public int count() {
            int n = 0;
            for (String name : EventImpl.EventCatalogImpl.EVENT_NAMES)
                if (Native.eventId(rt.handle(), name) >= 0) n++;
            return n;
        }
    }

    /* ---- item 索引 ---- */
    private final class ItemIndexImpl implements MosaicItemIndex {
        public MosaicItemDescriptor find(int category, String name) {
            long d = Native.itemDescriptor(rt.handle(), category, name);
            if (d == 0) return null;
            return new ItemDescriptorImpl(rt, d);
        }
        public void forEach(int category, Consumer<MosaicItemDescriptor> consumer) {
            long[] ds = Native.itemForEachCategory(rt.handle(), category);
            if (ds == null) return;
            for (long d : ds) consumer.accept(new ItemDescriptorImpl(rt, d));
        }
        public long count() { return Native.itemCount(rt.handle()); }
    }

    /** item 描述符:字段直通 mi_* 访问器(记录指针跨 JNI 边界保持有效)。 */
    static final class ItemDescriptorImpl implements MosaicItemDescriptor {
        private final long providerFnId;
        private final String name, tags;
        private final int category;
        private final String iconRef;
        ItemDescriptorImpl(RuntimeImpl rt, long d) {
            long h = rt.handle();
            this.providerFnId = Native.itemDescField(h, d, 0);
            this.category = (int) Native.itemDescField(h, d, 1);
            this.name = Native.itemDescString(h, d, 0);
            this.tags = Native.itemDescString(h, d, 1);
            this.iconRef = Native.itemDescString(h, d, 2);
        }
        public long providerFnId() { return providerFnId; }
        public String name() { return name; }
        public String tags() { return tags; }
        public int category() { return category; }
        public String iconRef() { return iconRef; }
    }
}
