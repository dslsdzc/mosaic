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
#include "mosaic/base.h"
#include "mosaic/runtime.h"
#include "mosaic/event.h"

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
