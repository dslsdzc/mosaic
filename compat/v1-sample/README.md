# v1 API 兼容样例(M5-5)

**只增不减的机器保证**:本样例只使用 `API_VERSION 1` 引入的 API 成员。
任何 v1 签名被删除/修改 → 样例编译失败 → `ci/gates.sh` 门禁红。
编译成功是兼容性的第一道门(行为上的第二道门是 `ApiContractTest` 契约套件)。

样例覆盖的 v1 API 面:版本守卫(`MosaicApi.requireApi`)、运行时入口
(`MosaicRuntime.open/functionCount/eventId/eventDispatch/close`)、
函数生命周期(`MosaicFunctionLifecycle.materialize/state`)。

## 运行

```bash
bash compat/v1-sample/run.sh
```

自包含:生成测试 pack → 编译 japi(Bridge + java-api)+ 样例 → 运行。
期望输出末尾:

```
V1 SAMPLE OK: functions=3 dispatch=2 state0=0
```

配合版本守卫测试 `tests/jni/ApiVersionTest.java`(声明超版本 → 拒绝)
构成 M5 兼容套件,由 `ci/gates.sh` 在全部既有门禁之后统一执行。
