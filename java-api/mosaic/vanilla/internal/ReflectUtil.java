package mosaic.vanilla.internal;

import java.lang.reflect.*;

/** 反射调用工具:经版本映射表调用原版类(类名/方法名在映射表,Provider 代码不硬编码)。
 *  Task 6 扩充:fieldStatic(静态字段读取——Blocks.STONE/Items.DIAMOND/BuiltInRegistries.*
 *  是字段而非方法,简报骨架的 callStatic("Blocks","STONE") 无法命中)与
 *  callConstructor(按参数匹配构造器)。 */
public final class ReflectUtil {
    private ReflectUtil() {}

    public static Object callStatic(String cls, String method, Object... args) throws Exception {
        Class<?> c = Class.forName(cls);
        for (Method m : c.getDeclaredMethods()) {
            if (m.getName().equals(method) && m.getParameterCount() == args.length && matches(m, args)) {
                m.setAccessible(true);
                return m.invoke(null, args);
            }
        }
        throw new NoSuchMethodException(cls + "." + method);
    }
    public static Object call(Object target, String method, Object... args) throws Exception {
        for (Method m : target.getClass().getMethods()) {
            if (m.getName().equals(method) && m.getParameterCount() == args.length && matches(m, args)) {
                m.setAccessible(true);
                return m.invoke(target, args);
            }
        }
        throw new NoSuchMethodException(target.getClass() + "." + method);
    }
    public static Object field(Object target, String name) throws Exception {
        for (Field f : target.getClass().getFields()) {
            if (f.getName().equals(name)) { f.setAccessible(true); return f.get(target); }
        }
        for (Field f : target.getClass().getDeclaredFields()) {
            if (f.getName().equals(name)) { f.setAccessible(true); return f.get(target); }
        }
        throw new NoSuchFieldException(target.getClass() + "." + name);
    }
    /** 静态字段读取(经类名;如 Blocks.STONE、BuiltInRegistries.BLOCK)。 */
    public static Object fieldStatic(String cls, String name) throws Exception {
        Class<?> c = Class.forName(cls);
        for (Field f : c.getFields()) {
            if (f.getName().equals(name)) { f.setAccessible(true); return f.get(null); }
        }
        for (Field f : c.getDeclaredFields()) {
            if (f.getName().equals(name)) { f.setAccessible(true); return f.get(null); }
        }
        throw new NoSuchFieldException(cls + "." + name);
    }
    /** 构造器调用(按参数个数与类型匹配;如 new BlockPos(x,y,z))。 */
    public static Object callConstructor(String cls, Object... args) throws Exception {
        Class<?> c = Class.forName(cls);
        for (Constructor<?> k : c.getConstructors()) {
            if (k.getParameterCount() == args.length && matches(k, args)) {
                k.setAccessible(true);
                return k.newInstance(args);
            }
        }
        throw new NoSuchMethodException(cls + ".<init>");
    }
    public static boolean hasClass(String cls) {
        try { Class.forName(cls); return true; } catch (ClassNotFoundException e) { return false; }
    }
    private static boolean matches(Method m, Object[] args) {
        Class<?>[] p = m.getParameterTypes();
        for (int i = 0; i < args.length; i++) {
            if (args[i] == null) continue;
            if (!box(p[i]).isAssignableFrom(args[i].getClass())) return false;
        }
        return true;
    }
    private static boolean matches(Constructor<?> k, Object[] args) {
        Class<?>[] p = k.getParameterTypes();
        for (int i = 0; i < args.length; i++) {
            if (args[i] == null) continue;
            if (!box(p[i]).isAssignableFrom(args[i].getClass())) return false;
        }
        return true;
    }
    private static Class<?> box(Class<?> c) {
        if (!c.isPrimitive()) return c;
        if (c == int.class) return Integer.class; if (c == long.class) return Long.class;
        if (c == double.class) return Double.class; if (c == float.class) return Float.class;
        if (c == boolean.class) return Boolean.class; if (c == short.class) return Short.class;
        if (c == byte.class) return Byte.class; if (c == char.class) return Character.class;
        return c;
    }
}
