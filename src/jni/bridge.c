/* src/jni/bridge.c — M4-1:JVM ↔ C 运行时双向通道(JNI 实现)。
 *
 * 稳定 ABI 面:Java 侧 mosaic.Bridge(java/mosaic/Bridge.java)逐方法直映射
 * mosaic_runtime_* / mosaic_event_dispatch(设计规格第 24 节:
 * Minecraft JVM ↕ Minimal Bridge ↕ C Runtime;Stable Runtime ABI)。
 *
 * 载荷约定:Java byte[] 与 C 事件载荷结构体(include/mosaic/events.h)字节
 * 序一致(小端),长度 == 对应结构体大小;bridge 不校验长度(派发只读载荷),
 * 约定由 Java 侧文档注释承担。
 *
 * 错误处理:句柄 0(非法/未打开)在进入 C 核心前短路——全部安全返回
 * 0 / -1,不崩;GetStringUTFChars / GetByteArrayElements 失败 → 抛
 * java.lang.OutOfMemoryError。
 */
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mosaic/base.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"
#include "mosaic/events.h"   /* M6-D:目录访问器(契约门禁) */
#include "mosaic/packets.h"  /* M6-E:包目录访问器(契约门禁) */
/* M5-2:函数生命周期(pack.h/function.h)、item 描述符(descriptor.h)、驱逐
   (eviction.h)、租约(ownership.h)、依赖(deps.h)、事务(tx.h)、以及内部
   查询/模块装载(find_function_active/find_module_ex/mod_load/mod_unload,
   mosaic_internal.h——该头在 src/,CMake 已追加包含路径) */
#include "mosaic/pack.h"
#include "mosaic/function.h"
#include "mosaic/descriptor.h"
#include "mosaic/eviction.h"
#include "mosaic/ownership.h"
#include "mosaic/deps.h"
#include "mosaic/tx.h"
#include "mosaic_internal.h"

/* jlong(64 位)句柄 ↔ mosaic_runtime*;0 即 NULL,不做任何解引用 */
static mosaic_runtime *rt_of(jlong h) { return (mosaic_runtime *)(intptr_t)h; }

static void throw_oom(JNIEnv *env) {
  jclass oom = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
  if (oom) (*env)->ThrowNew(env, oom, "JNI native allocation failed");
}

/* 打开 pack 组(String[] → open_many);失败返回 0(错误码经 lastError)。 */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_runtimeOpen(JNIEnv *env, jclass cls,
                                                       jobjectArray paths) {
  (void)cls;
  if (!paths) return 0;
  jsize n = (*env)->GetArrayLength(env, paths);
  if (n < 0) return 0;
  /* 空数组 = 零 pack,直接让 open_many 决定(应失败,返回 0) */
  const char **cpaths = (const char **)calloc((size_t)n ? (size_t)n : 1u, sizeof(char *));
  if (!cpaths) { throw_oom(env); return 0; }
  mosaic_runtime *rt = NULL;
  for (jsize i = 0; i < n; i++) {
    jstring js = (jstring)(*env)->GetObjectArrayElement(env, paths, i);
    if (!js) { rt = NULL; break; }                 /* 元素非 String → 失败 */
    cpaths[i] = (*env)->GetStringUTFChars(env, js, NULL);
    if (!cpaths[i]) { throw_oom(env); rt = NULL; break; }
  }
  if (n > 0 && cpaths[0]) {
    char errbuf[256];
    rt = mosaic_runtime_open_many(cpaths, (size_t)n, errbuf, sizeof errbuf);
  }
  for (jsize i = 0; i < n; i++) {
    if (!cpaths[i]) continue;
    jstring js = (jstring)(*env)->GetObjectArrayElement(env, paths, i);
    if (js) (*env)->ReleaseStringUTFChars(env, js, cpaths[i]);
  }
  free(cpaths);
  return rt ? (jlong)(intptr_t)rt : 0;
}

/* 关闭运行时;0 句柄为空操作。 */
JNIEXPORT void JNICALL Java_mosaic_Bridge_runtimeClose(JNIEnv *env, jclass cls,
                                                       jlong rt) {
  (void)env; (void)cls;
  if (rt != 0) mosaic_runtime_close(rt_of(rt));
}

/* 函数总数;0 句柄 → 0。 */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_functionCount(JNIEnv *env, jclass cls,
                                                         jlong rt) {
  (void)env; (void)cls;
  return rt != 0 ? (jlong)mosaic_runtime_function_count(rt_of(rt)) : 0;
}

/* 事件名 → id;未注册(或 0 句柄)返回 -1(C 侧 U32_NONE 哨兵映射到 -1)。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_eventId(JNIEnv *env, jclass cls,
                                                  jlong rt, jstring name) {
  (void)cls;
  if (rt == 0 || !name) return -1;
  const char *cname = (*env)->GetStringUTFChars(env, name, NULL);
  if (!cname) { throw_oom(env); return -1; }
  u32 id = mosaic_runtime_event_id(rt_of(rt), cname);
  (*env)->ReleaseStringUTFChars(env, name, cname);
  return id == MOSAIC_U32_NONE ? -1 : (jint)id;
}

/* 派发事件;payload byte[] → 临时缓冲 → mosaic_event_dispatch;返回执行数。
   0 句柄 / 空载荷 → 0(不崩)。载荷长度约定见 Bridge.java 类注释。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_eventDispatch(JNIEnv *env, jclass cls,
                                                        jlong rt, jint eventId,
                                                        jbyteArray payload) {
  (void)cls;
  if (rt == 0 || !payload) return 0;
  jbyte *buf = (*env)->GetByteArrayElements(env, payload, NULL);
  if (!buf) { throw_oom(env); return 0; }
  u32 executed = mosaic_event_dispatch(rt_of(rt), (u32)eventId, buf);
  (*env)->ReleaseByteArrayElements(env, payload, buf, JNI_ABORT); /* 只读,不拷贝回 */
  return (jint)executed;
}

/* 工作集大小;0 句柄 → 0。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_workingSetCount(JNIEnv *env, jclass cls,
                                                          jlong rt) {
  (void)env; (void)cls;
  return rt != 0 ? (jint)mosaic_runtime_working_set_count(rt_of(rt)) : 0;
}

/* M4-3:世界内动态加载——向已打开实例追加 pack(零重启);0 成功,-1 失败
   (错误码经 lastError;C 侧 errbuf 文案为诊断细节,错误语义取错误码)。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_runtimeAddPack(JNIEnv *env, jclass cls,
                                                         jlong rt, jstring packPath) {
  (void)cls;
  if (rt == 0 || !packPath) return -1;
  const char *cpath = (*env)->GetStringUTFChars(env, packPath, NULL);
  if (!cpath) { throw_oom(env); return -1; }
  char errbuf[256];
  int rc = mosaic_runtime_add_pack(rt_of(rt), cpath, errbuf, sizeof errbuf);
  (*env)->ReleaseStringUTFChars(env, packPath, cpath);
  return rc == 0 ? 0 : -1;
}

/* 已挂载 pack 数;0 句柄 → 0。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_packCount(JNIEnv *env, jclass cls,
                                                    jlong rt) {
  (void)env; (void)cls;
  return rt != 0 ? (jint)mosaic_runtime_pack_count(rt_of(rt)) : 0;
}

/* 最后错误码;0 句柄 → 0(C 核心对 NULL rt 返回 ERR_IO,不穿过)。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_lastError(JNIEnv *env, jclass cls,
                                                    jlong rt) {
  (void)env; (void)cls;
  return rt != 0 ? (jint)mosaic_runtime_last_error(rt_of(rt)) : 0;
}

/* M9:每事件派发超时预算(微秒;0 = 不限制,默认 0)。0 句柄为安全空操作。
   语义见 runtime.h:mosaic_runtime_set_dispatch_timeout 声明注释。 */
JNIEXPORT void JNICALL Java_mosaic_Bridge_setDispatchTimeout(JNIEnv *env, jclass cls,
                                                             jlong rt, jlong us) {
  (void)env; (void)cls;
  if (rt != 0) mosaic_runtime_set_dispatch_timeout(rt_of(rt), (u64)us);
}

/* M6-D N2:事件目录访问器(契约门禁)。返回目录第 index 个事件名(静态字符
   串,NewStringUTF 拷贝);越界(含负 index 经 u32 包装)→ NULL。 */
JNIEXPORT jstring JNICALL Java_mosaic_Bridge_eventCatalogName(JNIEnv *env, jclass cls,
                                                              jint index) {
  (void)cls;
  const char *n = mosaic_event_catalog_name((u32)index);
  return n ? (*env)->NewStringUTF(env, n) : NULL;
}

/* M6-E N2:包目录访问器(契约门禁)。返回目录第 index 个包名(静态字符串,
   NewStringUTF 拷贝);越界(含负 index 经 u32 包装)→ NULL。Java 契约测试
   遍历全部名字与 PacketCatalogImpl.PACKET_NAMES 逐项比对——packets.c 增删
   目录名 → Java 比对失败 → 测试红,防包目录跨语言漂移。 */
JNIEXPORT jstring JNICALL Java_mosaic_Bridge_packetCatalogName(JNIEnv *env, jclass cls,
                                                               jint index) {
  (void)cls;
  const char *n = mosaic_packet_catalog_name((u32)index);
  return n ? (*env)->NewStringUTF(env, n) : NULL;
}

/* LC-3:包目录 id 访问器(公式门禁)。返回目录第 index 个条目的实际 id
   (packets.c id 列);越界(含负 index 经 u32 包装)→ 0(UNKNOWN=0 不入目录,
   0 即"无"哨兵)。Java 契约测试遍历全部条目,把公式(base+1+rank,分组表)
   重算值与目录实际 id 双向比对——任一侧漂移 → 测试红。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_packetCatalogId(JNIEnv *env, jclass cls,
                                                          jint index) {
  (void)env; (void)cls;
  return (jint)mosaic_packet_catalog_id((u32)index);
}

/* ===== M5:函数生命周期 ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnMaterialize(JNIEnv *env, jclass c, jlong rt_, jlong fnId) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  mosaic_fn_obj *fn = mosaic_fn_materialize(rt, (u64)fnId);
  return fn ? (jlong)(intptr_t)fn : 0;
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_fnTombstone(JNIEnv *env, jclass c, jlong rt_, jlong h) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt || !h) return -1;
  return mosaic_fn_tombstone(rt, (mosaic_fn_obj *)(intptr_t)h);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_fnExecute(JNIEnv *env, jclass c, jlong rt_, jlong h,
                                                    jint eventId, jbyteArray payload) {
  (void)rt_; (void)c;
  mosaic_fn_obj *fn = (mosaic_fn_obj *)(intptr_t)h;
  if (!fn) return;
  jbyte *buf = payload ? (*env)->GetByteArrayElements(env, payload, NULL) : NULL;
  jsize len = payload ? (*env)->GetArrayLength(env, payload) : 0;
  if (payload && !buf) return;   /* OOM,VM 已抛 */
  /* 载荷栈缓冲(事件载荷 ≤ 64B) */
  u8 tmp[64]; const void *ev = tmp;
  if (buf && (size_t)len <= sizeof tmp) memcpy(tmp, buf, (size_t)len); else ev = buf;
  mosaic_fn_execute(fn, (u32)eventId, ev);
  if (payload) (*env)->ReleaseByteArrayElements(env, payload, buf, JNI_ABORT);
}
JNIEXPORT jbyteArray JNICALL Java_mosaic_Bridge_fnState(JNIEnv *env, jclass c, jlong rt_, jlong h) {
  (void)rt_; (void)c;
  mosaic_fn_obj *fn = (mosaic_fn_obj *)(intptr_t)h;
  if (!fn || !fn->state) return NULL;
  jbyteArray out = (*env)->NewByteArray(env, (jsize)fn->state_size);
  if (!out) return NULL;
  (*env)->SetByteArrayRegion(env, out, 0, (jsize)fn->state_size, (const jbyte *)fn->state);
  return out;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnIdOf(JNIEnv *env, jclass c, jlong rt_, jlong h) {
  (void)env; (void)c; (void)rt_;
  mosaic_fn_obj *fn = (mosaic_fn_obj *)(intptr_t)h;
  return fn ? (jlong)fn->fn_id : 0;
}
/* M6-B:写函数状态(物化后)。校验:句柄有效、state 非空、len <= fn->state_size
   (超长拒绝);通过则 memcpy len 字节到 fn->state(尾随字节保持原样,语义 =
   Java 侧按 state_size 整块写)。失败 → -1 + last_err(ILLEGAL)。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_fnStateWrite(JNIEnv *env, jclass c, jlong rt_, jlong h,
                                                       jbyteArray state) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  mosaic_fn_obj *fn = (mosaic_fn_obj *)(intptr_t)h;
  if (!rt || !fn || !fn->state || !state) return -1;
  jsize len = (*env)->GetArrayLength(env, state);
  if (len < 0 || (u32)len > fn->state_size) { rt->last_err = MOSAIC_ERR_ILLEGAL; return -1; }
  jbyte *buf = (*env)->GetByteArrayElements(env, state, NULL);
  if (!buf) { throw_oom(env); return -1; }
  memcpy(fn->state, buf, (size_t)len);
  (*env)->ReleaseByteArrayElements(env, state, buf, JNI_ABORT); /* 只读源,不拷贝回 */
  return 0;
}
/* M6-B:列出事件订阅者 fn_id(触发表区间扫描;与 dispatch 同纪律:仅基础 pack,
   逐 pack 现算 map + trigger_lower_bound 二分到事件区间,再顺序收集)。
   out == NULL → 探测模式,返回订阅者总数;out != NULL → 填充 min(cap, 总数)
   条,返回实际写入数(容量不足截断)。无订阅/事件未注册 → 0。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_triggerSubscribers(JNIEnv *env, jclass c, jlong rt_,
                                                             jint eventId, jlongArray out) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return -1;
  if (!out) {                          /* 探测:总数 */
    u64 total = 0;
    for (size_t p = 0; p < rt->n_packs; p++) {
      u8 *map = pack_map(rt, p);
      u64 n = hdr_trigger_count(map);
      u64 i = trigger_lower_bound(map, (u32)eventId);
      const mosaic_trigger_entry *t =
          (const mosaic_trigger_entry *)(map + hdr_trigger_off(map));
      while (i < n && mt_event_id(&t[i]) == (u32)eventId) { total++; i++; }
    }
    return (jint)total;
  }
  jsize cap = (*env)->GetArrayLength(env, out);
  if (cap <= 0) return 0;
  jlong *buf = (*env)->GetLongArrayElements(env, out, NULL);
  if (!buf) { throw_oom(env); return -1; }
  jsize written = 0;
  for (size_t p = 0; p < rt->n_packs && written < cap; p++) {
    u8 *map = pack_map(rt, p);
    u64 n = hdr_trigger_count(map);
    u64 i = trigger_lower_bound(map, (u32)eventId);
    const mosaic_trigger_entry *t =
        (const mosaic_trigger_entry *)(map + hdr_trigger_off(map));
    while (i < n && mt_event_id(&t[i]) == (u32)eventId && written < cap) {
      buf[written++] = (jlong)mt_fn_id(&t[i]);
      i++;
    }
  }
  (*env)->ReleaseLongArrayElements(env, out, buf, 0);
  return (jint)written;
}
/* ===== M5:pack 构建器(直通 C builder) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_packCreate(JNIEnv *env, jclass c, jstring path,
    jlong mc, jlong fc, jlong tc, jlong dc, jint ec) {
  const char *p = path ? (*env)->GetStringUTFChars(env, path, NULL) : NULL;
  if (path && !p) return 0;
  mosaic_pack_builder *b = mosaic_pack_builder_create(p, (u64)mc, (u64)fc, (u64)tc, (u64)dc, (u32)ec);
  if (path) (*env)->ReleaseStringUTFChars(env, path, p);
  return b ? (jlong)(intptr_t)b : 0;
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddEvent(JNIEnv *env, jclass c, jlong b_, jstring name) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
  if (name && !n) return;
  mosaic_pack_builder_add_event(b, n);
  if (name) (*env)->ReleaseStringUTFChars(env, name, n);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddModule(JNIEnv *env, jclass c, jlong b_, jlong mid,
    jint ver, jstring name, jstring so) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
  const char *s = so ? (*env)->GetStringUTFChars(env, so, NULL) : NULL;
  if ((name && !n) || (so && !s)) { if (n) (*env)->ReleaseStringUTFChars(env, name, n); return; }
  mosaic_pack_builder_add_module(b, (u64)mid, (u32)ver, n, s);
  if (n) (*env)->ReleaseStringUTFChars(env, name, n);
  if (s) (*env)->ReleaseStringUTFChars(env, so, s);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddFn(JNIEnv *env, jclass c, jlong b_, jlong mid,
    jlong local, jint codeOff, jint stateSize, jint gen, jint cost, jint flags) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_add_fn(b, (u64)mid, (u64)local, (u32)codeOff, (u32)stateSize,
                             (u32)gen, (u32)cost, (u16)flags);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packSetFnTransform(JNIEnv *env, jclass c, jlong b_,
    jlong fnId, jint idx) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_set_fn_transform(b, (u64)fnId, (u32)idx);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddTrigger(JNIEnv *env, jclass c, jlong b_,
    jint eventId, jlong fnId) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_add_trigger(b, (u32)eventId, (u64)fnId);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddDep(JNIEnv *env, jclass c, jlong b_,
    jlong owner, jlong dep) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_add_dep(b, (u64)owner, (u64)dep);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packSetItemCount(JNIEnv *env, jclass c, jlong b_, jlong n) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  mosaic_pack_builder_set_item_count(b, (u64)n);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packAddItem(JNIEnv *env, jclass c, jlong b_,
    jlong provider, jstring name, jstring tags, jint category, jstring icon, jint flags) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return;
  const char *n = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
  const char *t = tags ? (*env)->GetStringUTFChars(env, tags, NULL) : NULL;
  const char *i = icon ? (*env)->GetStringUTFChars(env, icon, NULL) : NULL;
  /* OOM 失败路径必须释放全部已获取字符串(Task 5 评审修复:第 3 个
     GetStringUTFChars 失败时 t 已获取——原先只释放 n,t 泄漏) */
  if ((name && !n) || (tags && !t) || (icon && !i)) {
    if (n) (*env)->ReleaseStringUTFChars(env, name, n);
    if (t) (*env)->ReleaseStringUTFChars(env, tags, t);
    return;
  }
  mosaic_pack_builder_add_item(b, (u64)provider, n, t, (u32)category, i, (u32)flags);
  if (n) (*env)->ReleaseStringUTFChars(env, name, n);
  if (t) (*env)->ReleaseStringUTFChars(env, tags, t);
  if (i) (*env)->ReleaseStringUTFChars(env, icon, i);
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_packFinish(JNIEnv *env, jclass c, jlong b_) {
  mosaic_pack_builder *b = (mosaic_pack_builder *)(intptr_t)b_;
  if (!b) return -1;
  char err[256];
  return mosaic_pack_builder_finish(b, err, sizeof err);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_packFree(JNIEnv *env, jclass c, jlong b_) {
  (void)env; (void)c;
  mosaic_pack_builder_free((mosaic_pack_builder *)(intptr_t)b_);
}
/* ===== M5:描述符查询(直通 mmap 访问器) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnDescriptor(JNIEnv *env, jclass c, jlong rt_, jlong fnId) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  size_t pack = 0;
  const mosaic_function_record *r = find_function_active(rt, (u64)fnId, &pack);
  return r ? (jlong)(intptr_t)r : 0;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_fnDescField(JNIEnv *env, jclass c, jlong rt_, jlong d, jint field) {
  (void)env; (void)c; (void)rt_;
  const mosaic_function_record *r = (const mosaic_function_record *)(intptr_t)d;
  if (!r) return -1;
  switch (field) {
    case 0: return (jlong)mf_id(r);
    case 1: return (jlong)mf_module_id(r);
    case 2: return (jlong)mf_code_off(r);
    case 3: return (jlong)mf_generation(r);
    case 4: return (jlong)mf_state_size(r);
    case 5: return (jlong)mf_cost_hint(r);
    case 6: return (jlong)mf_flags(r);
  }
  return -1;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_moduleDescriptor(JNIEnv *env, jclass c, jlong rt_, jlong moduleId) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  size_t pack = 0;
  const mosaic_module_record *m = find_module_ex(rt, (u64)moduleId, &pack);
  return m ? (jlong)(intptr_t)m : 0;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_modDescField(JNIEnv *env, jclass c, jlong rt_, jlong d, jint field) {
  (void)env; (void)c; (void)rt_;
  const mosaic_module_record *m = (const mosaic_module_record *)(intptr_t)d;
  if (!m) return -1;
  switch (field) {
    case 0: return (jlong)mm_id(m);
    case 1: return (jlong)mm_version(m);
    case 2: return (jlong)mm_generation(m);
    case 3: return (jlong)mm_fn_count(m);   /* M6-C:fnCount(模块记录内计数) */
  }
  return -1;
}
JNIEXPORT jstring JNICALL Java_mosaic_Bridge_modDescString(JNIEnv *env, jclass c, jlong rt_, jlong d, jint field) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  const mosaic_module_record *m = (const mosaic_module_record *)(intptr_t)d;
  if (!rt || !m) return NULL;
  u32 off = field == 0 ? mm_name_off(m) : mm_so_off(m);
  const char *s = mosaic_runtime_module_string(rt, m, off);
  return s ? (*env)->NewStringUTF(env, s) : NULL;
}

/* ===== M5:item 描述符查询(直通 descriptor.c,冷态零物化) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_itemCount(JNIEnv *env, jclass c, jlong rt_) {
  (void)env; (void)c;
  return (jlong)mosaic_item_count((mosaic_runtime *)(intptr_t)rt_);
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_itemDescriptor(JNIEnv *env, jclass c, jlong rt_,
                                                          jint category, jstring name) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt || !name) return 0;
  const char *n = (*env)->GetStringUTFChars(env, name, NULL);
  if (!n) return 0;
  const mosaic_item_record *it = mosaic_item_by_name(rt, (u32)category, n);
  (*env)->ReleaseStringUTFChars(env, name, n);
  return it ? (jlong)(intptr_t)it : 0;
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_itemDescField(JNIEnv *env, jclass c, jlong rt_,
                                                         jlong d, jint field) {
  (void)env; (void)c; (void)rt_;
  const mosaic_item_record *it = (const mosaic_item_record *)(intptr_t)d;
  if (!it) return -1;
  switch (field) {
    case 0: return (jlong)mi_provider(it);
    case 1: return (jlong)mi_category(it);
  }
  return -1;
}
JNIEXPORT jstring JNICALL Java_mosaic_Bridge_itemDescString(JNIEnv *env, jclass c, jlong rt_,
                                                            jlong d, jint field) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  const mosaic_item_record *it = (const mosaic_item_record *)(intptr_t)d;
  if (!rt || !it) return NULL;
  const char *s = NULL;
  switch (field) {
    case 0: s = mosaic_item_name(rt, it); break;
    case 1: s = mosaic_item_tags(rt, it); break;
    case 2: s = mosaic_item_icon(rt, it); break;
    default: return NULL;
  }
  return s ? (*env)->NewStringUTF(env, s) : NULL;
}
/* 分类内全部 item 记录指针(mosaic_item_for_each 回调收集;Java 侧逐个包装
   为 MosaicItemDescriptor——枚举路径零物化,与 by_name 同纪律)。 */
typedef struct { const mosaic_item_record **arr; size_t n, cap; } item_collect;
static int item_collect_cb(const mosaic_item_record *item, void *user) {
  item_collect *col = (item_collect *)user;
  if (col->n == col->cap) {
    size_t nc = col->cap ? col->cap * 2 : 8;
    const mosaic_item_record **na =
        (const mosaic_item_record **)realloc(col->arr, nc * sizeof *na);
    if (!na) return 1;   /* 非 0 → for_each 停止并透传 */
    col->arr = na; col->cap = nc;
  }
  col->arr[col->n++] = item;
  return 0;
}
JNIEXPORT jlongArray JNICALL Java_mosaic_Bridge_itemForEachCategory(JNIEnv *env, jclass c,
                                                                    jlong rt_, jint category) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return NULL;
  item_collect col = { NULL, 0, 0 };
  if (mosaic_item_for_each(rt, (u32)category, item_collect_cb, &col) != 0) {
    free(col.arr);
    return NULL;
  }
  jlongArray out = (*env)->NewLongArray(env, (jsize)col.n);
  if (out && col.n > 0) {
    jlong *buf = (*env)->GetLongArrayElements(env, out, NULL);
    if (buf) {
      for (size_t k = 0; k < col.n; k++) buf[k] = (jlong)(intptr_t)col.arr[k];
      (*env)->ReleaseLongArrayElements(env, out, buf, 0);
    }
  }
  free(col.arr);
  return out;
}

/* ===== M5:模块装载 / 计数(直通 mod_load/mod_unload 与 pack 头计数) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_moduleLoad(JNIEnv *env, jclass c, jlong rt_, jlong moduleId) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  const mosaic_module_abi *abi = mod_load(rt, (u64)moduleId);
  return abi ? (jlong)(intptr_t)abi : 0;
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_moduleUnload(JNIEnv *env, jclass c, jlong rt_, jlong moduleId) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return;
  mod_unload(rt, (u64)moduleId);
}
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_moduleCount(JNIEnv *env, jclass c, jlong rt_) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  u64 total = 0;
  for (size_t i = 0; i < rt->n_packs; i++) total += hdr_module_count(rt->packs[i].map);
  return (jlong)total;
}

/* ===== M5:依赖闭包解析(直通 mosaic_dep_resolve;两阶段:探测长度 → 填充) ===== */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_depResolve(JNIEnv *env, jclass c, jlong rt_,
    jlong moduleId, jint minVer, jint maxVer, jlongArray out) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return -1;
  mosaic_version_constraint con = { (u32)minVer, (u32)maxVer };
  size_t len = 0;
  if (mosaic_dep_resolve(rt, (u64)moduleId, &con, NULL, 0, &len) != 0) return -1;
  if (!out) return (jint)len;                    /* 探测模式 */
  jsize cap = (*env)->GetArrayLength(env, out);
  if (cap <= 0) return (jint)len;
  jlong *buf = (*env)->GetLongArrayElements(env, out, NULL);
  if (!buf) { throw_oom(env); return -1; }
  size_t n = 0;
  int rc = mosaic_dep_resolve(rt, (u64)moduleId, &con, (u64 *)buf, (size_t)cap, &n);
  (*env)->ReleaseLongArrayElements(env, out, buf, rc == 0 ? 0 : JNI_ABORT);
  return rc == 0 ? (jint)n : -1;
}

/* ===== M5:驱逐 / 工作集快照 ===== */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_evictIdle(JNIEnv *env, jclass c, jlong rt_, jlong windowNanos) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  mosaic_evict_config cfg = { (u64)windowNanos };
  return (jint)mosaic_evict_idle(rt, &cfg);
}
JNIEXPORT jlongArray JNICALL Java_mosaic_Bridge_activeFnIds(JNIEnv *env, jclass c, jlong rt_) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return NULL;
  jlongArray out = (*env)->NewLongArray(env, (jsize)rt->ws.len);
  if (!out) return NULL;
  if (rt->ws.len == 0) return out;
  jlong *buf = (*env)->GetLongArrayElements(env, out, NULL);
  if (!buf) { throw_oom(env); return NULL; }
  size_t k = 0;
  for (u64 i = 0; i < rt->ws.cap; i++)
    if (rt->ws.keys[i] != 0) buf[k++] = (jlong)rt->ws.keys[i];
  (*env)->ReleaseLongArrayElements(env, out, buf, 0);
  return out;
}

/* ===== M5:资源租约(直通 mosaic_lease_acquire/release) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_leaseAcquire(JNIEnv *env, jclass c, jlong rt_, jlong fnId) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  mosaic_lease *l = mosaic_lease_acquire(rt, (u64)fnId);
  return l ? (jlong)(intptr_t)l : 0;
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_leaseRelease(JNIEnv *env, jclass c, jlong lease) {
  (void)env; (void)c;
  mosaic_lease_release((mosaic_lease *)(intptr_t)lease);
}

/* ===== M5:补丁事务(直通 mosaic_tx_*;free 是唯一释放入口,commit/rollback/
       abort 后必须 txFree) ===== */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_txBegin(JNIEnv *env, jclass c, jlong rt_, jstring path) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt || !path) return 0;
  const char *p = (*env)->GetStringUTFChars(env, path, NULL);
  if (!p) return 0;
  char errbuf[256];
  mosaic_tx *tx = mosaic_tx_begin(rt, p, errbuf, sizeof errbuf);
  (*env)->ReleaseStringUTFChars(env, path, p);
  return tx ? (jlong)(intptr_t)tx : 0;
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_txValidate(JNIEnv *env, jclass c, jlong tx) {
  (void)env; (void)c;
  if (!tx) return -1;
  char errbuf[256];
  return mosaic_tx_validate((mosaic_tx *)(intptr_t)tx, errbuf, sizeof errbuf) == 0 ? 0 : -1;
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_txCommit(JNIEnv *env, jclass c, jlong tx) {
  (void)env; (void)c;
  if (!tx) return -1;
  char errbuf[256];
  return mosaic_tx_commit((mosaic_tx *)(intptr_t)tx, errbuf, sizeof errbuf) == 0 ? 0 : -1;
}
JNIEXPORT jint JNICALL Java_mosaic_Bridge_txRollback(JNIEnv *env, jclass c, jlong tx) {
  (void)env; (void)c;
  if (!tx) return -1;
  char errbuf[256];
  return mosaic_tx_rollback((mosaic_tx *)(intptr_t)tx, errbuf, sizeof errbuf) == 0 ? 0 : -1;
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_txAbort(JNIEnv *env, jclass c, jlong tx) {
  (void)env; (void)c;
  mosaic_tx_abort((mosaic_tx *)(intptr_t)tx);
}
JNIEXPORT void JNICALL Java_mosaic_Bridge_txFree(JNIEnv *env, jclass c, jlong tx) {
  (void)env; (void)c;
  mosaic_tx_free((mosaic_tx *)(intptr_t)tx);
}

/* ===== M6-C:元数据(直接依赖遍历 / pack 计数 / tx 补丁 fn 表) ===== */

/* 直接依赖遍历:moduleId 的依赖模块 id 列表(单层,非闭包;闭包见 depResolve)。
   定位模块记录(find_module_ex)→ module_dep_range 取本 pack 依赖表区间 →
   收集 md_dep_id。out == NULL → 探测模式返回总数;out != NULL → 填充
   min(cap, 总数) 条并返回实际写入数(容量不足截断,与 triggerSubscribers
   同款两阶段纪律)。模块不存在 → -1(last_err 由 find_module_ex 置)。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_depForEach(JNIEnv *env, jclass c, jlong rt_,
                                                     jlong moduleId, jlongArray out) {
  (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return -1;
  size_t pack = 0;
  const mosaic_module_record *m = find_module_ex(rt, (u64)moduleId, &pack);
  if (!m) return -1;
  u64 s, e;
  module_dep_range(rt, pack, m, &s, &e);
  if (s == e) return 0;                    /* 无依赖 → 空 */
  if (!out) return (jint)(e - s);          /* 探测:总数 */
  jsize cap = (*env)->GetArrayLength(env, out);
  if (cap <= 0) return (jint)(e - s);
  jlong *buf = (*env)->GetLongArrayElements(env, out, NULL);
  if (!buf) { throw_oom(env); return -1; }
  const u8 *map = rt->packs[pack].map;
  const mosaic_dep_entry *deps = (const mosaic_dep_entry *)(map + hdr_dep_off(map));
  jsize written = 0;
  for (u64 i = s; i < e && written < cap; i++) buf[written++] = (jlong)md_dep_id(&deps[i]);
  (*env)->ReleaseLongArrayElements(env, out, buf, 0);
  return (jint)written;
}

/* pack 信息:5 计数一次返回(field 0=module 1=fn 2=trigger 3=item 4=event;
   全部基础 pack 合并求和,与 moduleCount/functionCount/itemCount 同视图;
   eventCount 每 pack u32,跨 pack 求和可能溢出 u32——以 jlong 返回,
   Java 侧按接口 int 截断)。非法 field → -1。 */
JNIEXPORT jlong JNICALL Java_mosaic_Bridge_packInfoCount(JNIEnv *env, jclass c, jlong rt_,
                                                         jint field) {
  (void)env; (void)c;
  mosaic_runtime *rt = (mosaic_runtime *)(intptr_t)rt_;
  if (!rt) return 0;
  u64 total = 0;
  for (size_t i = 0; i < rt->n_packs; i++) {
    const u8 *map = rt->packs[i].map;
    switch (field) {
      case 0: total += hdr_module_count(map); break;
      case 1: total += hdr_fn_count(map); break;
      case 2: total += hdr_trigger_count(map); break;
      case 3: total += hdr_item_count(map); break;
      case 4: total += hdr_event_count(map); break;
      default: return -1;
    }
  }
  return (jlong)total;
}

/* tx 补丁函数 id 列表:枚举补丁 pack fn 表(mosaic_tx_patch_view 只读视图;
   补丁每 fn 单条且按 fn_id 排序——begin 已校验)。out == NULL → 探测返回
   总数;out != NULL → 填充 min(cap, 总数) 条返回实际写入数。 */
JNIEXPORT jint JNICALL Java_mosaic_Bridge_txPatchFnIds(JNIEnv *env, jclass c, jlong tx_,
                                                       jlongArray out) {
  (void)c;
  mosaic_tx *tx = (mosaic_tx *)(intptr_t)tx_;
  if (!tx) return -1;
  const struct pack_view *pv = mosaic_tx_patch_view(tx);
  if (!pv) return -1;
  u64 n = hdr_fn_count(pv->map);
  if (!out) return (jint)n;                /* 探测:总数 */
  jsize cap = (*env)->GetArrayLength(env, out);
  if (cap <= 0) return (jint)n;
  jlong *buf = (*env)->GetLongArrayElements(env, out, NULL);
  if (!buf) { throw_oom(env); return -1; }
  const mosaic_function_record *fns =
      (const mosaic_function_record *)(pv->map + hdr_fn_off(pv->map));
  jsize written = 0;
  for (u64 i = 0; i < n && written < cap; i++) buf[written++] = (jlong)mf_id(&fns[i]);
  (*env)->ReleaseLongArrayElements(env, out, buf, 0);
  return (jint)written;
}
