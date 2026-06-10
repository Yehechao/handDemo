# 主体项目四步校准修正计划

> 本文件是当前唯一沟通文档。  
> 本轮只审查和规划主体项目，不考虑打包、发布、CMakeLists.txt 和 SDK 化改造。  
> 另一个 agent 修改代码后，必须在本文件对应任务后面把 `[ ]` 改成 `[x]`，然后再交给审查。

## 一、当前结论

**审查结论：不通过。**

当前项目的校准动作必须是 4 个，并且顺序固定：

```text
Closed -> Fist -> Spread -> Crosstalk
```

其中 `Crosstalk` 不是可选动作，而是必须校准动作。四步全部完成后，算法才允许进入实时角度输出状态。

当前代码的问题是：`HandAngleAlgorithm::isReady()` 仍然只判断前三步：

```cpp
return hasClosed_ && hasFist_ && hasOpen_;
```

这会导致算法层认为 Closed/Fist/Spread 完成后已经 ready。`main.cpp` 又额外维护了 `hasCrosstalkCalibration` 来挡实时输出，形成了两套 ready 语义：

- 算法层：三步 ready。
- demo 主流程：三步 ready + 额外串扰 flag 才输出。

这个方向不对。正确方向是：**串扰补偿本身就是第四个校准动作，必须进入算法层 ready 判断，并且统一影响 `isReady()`。**

## 二、必须修正的主线逻辑

目标状态：

```text
Closed 完成 -> isReady() == false
Fist 完成 -> isReady() == false
Spread 完成 -> isReady() == false
Crosstalk 完成 -> isReady() == true
```

实时输出只看算法层 `isReady()`：

```text
processFrame() 内部使用 isReady()
main.cpp 输出门槛也只使用 algorithm.isReady()
```

不再在 `main.cpp` 维护独立的串扰完成状态。

## 三、修改任务清单

### 1. 算法层统一四步 ready 语义

- [x] 在 `HandAngleAlgorithm` 内部新增基础校准判断函数，例如 `hasBaseCalibration()` 或 `isBaseCalibrated()`。

建议逻辑：

```cpp
bool HandAngleAlgorithm::hasBaseCalibration() const {
    return hasClosed_ && hasFist_ && hasOpen_;
}
```

- [x] 将 `HandAngleAlgorithm::isReady()` 改为四步全部完成才返回 true。

目标逻辑：

```cpp
bool HandAngleAlgorithm::isReady() const {
    return hasBaseCalibration() && hasXtalk_;
}
```

- [x] 修改 `finishCalibration()` 中 Crosstalk 阶段的前置检查，不能继续调用 `isReady()`。

原因：如果 `isReady()` 包含 `hasXtalk_`，Crosstalk 自己开始前调用 `isReady()` 会永远失败。

目标逻辑：

```cpp
if (samplingState_.stage == CalibrationStage::Crosstalk) {
    if (!hasBaseCalibration()) {
        resetSamplingState();
        return false;
    }
    ...
}
```

- [x] 保持 `processFrame()` 继续使用 `isReady()` 判断，不增加额外条件。

### 2. 主流程删除 demo 层串扰 flag

- [x] 删除 `main.cpp` 中的 `hasCrosstalkCalibration` 变量。
- [x] 删除 `resetCalibration()` 参数中的 `bool& hasCrosstalk`，重置逻辑只保留算法和步骤计数。
- [x] 删除 Crosstalk 完成后对 `hasCrosstalkCalibration = true` 的赋值。
- [x] 实时输出门槛只保留：

```cpp
if (!serialPollResult.hasFrame ||
    samplingRuntimeState.isActive ||
    !algorithm.isReady()) {
    continue;
}
```

- [x] 保留四步引导文案：Spread 完成后提示继续 step4，Crosstalk 完成后才提示开始实时输出角度。

### 3. 文档和注释统一四步语义

- [x] 修改 `hand_algorithm.h` 中 `isReady()` 注释：四步校准全部完成后才返回 true。
- [x] 修改 `README.md` 中所有“三步即可 ready / Crosstalk 可选 / 三步即可输出”的描述。
- [x] 修改 `docs/api.md` 中 `matrix_hand_is_ready()` 和校准阶段说明，去掉 `Crosstalk（可选）`，改为四步完成后 ready。
- [x] 检查 `docs/calibration.md`，确保示例中 `is_ready` 只出现在 Crosstalk 完成之后；不要在 Spread 完成后写 ready。
- [x] 全项目搜索并清理错误语义：

```powershell
rg -n "可选|三步|基础校准|isReady|is_ready|ready|Crosstalk|串扰" README.md docs hand_algorithm.h main.cpp hand_algorithm.cpp
```

要求：不再出现“Crosstalk 可选”“三步即可 ready”“三步即可实时输出”的语义。

## 四、全链路验收标准

### 1. 代码静态验收

- [x] `hand_algorithm.cpp` 中 `isReady()` 必须包含 `hasXtalk_`。
- [x] `finishCalibration()` 的 Crosstalk 前置条件必须使用基础校准判断，不能使用四步 `isReady()`。
- [x] `processFrame()` 未 ready 时必须返回 `false`，并清零输出。
- [x] `main.cpp` 不再出现 `hasCrosstalkCalibration`。
- [x] `main.cpp` 实时输出门槛不再维护第二套 ready 语义。

检查命令：

```powershell
rg -n "hasCrosstalkCalibration" main.cpp
rg -n "bool HandAngleAlgorithm::isReady|hasXtalk_|hasBaseCalibration|isBaseCalibrated" hand_algorithm.cpp hand_algorithm.h
```

通过标准：

- 第一条命令无输出。
- 第二条命令能看到基础校准判断和四步 `isReady()`。

### 2. 行为验收

- [x] 只完成 Closed 后，`algorithm.isReady() == false`。
- [x] 完成 Closed/Fist 后，`algorithm.isReady() == false`。
- [x] 完成 Closed/Fist/Spread 后，`algorithm.isReady() == false`。
- [x] 完成 Closed/Fist/Spread 后直接调用 `processFrame()`，返回 `false`，输出为 0。
- [x] 完成 Crosstalk 且拟合成功后，`algorithm.isReady() == true`。
- [x] Crosstalk 拟合成功后调用 `processFrame()`，能进入串扰补偿后的角度计算链路。
- [x] Crosstalk 拟合失败时，`algorithm.isReady() == false`，不能实时输出角度。

### 3. 主流程验收

- [x] 控制台启动后提示四步校准顺序。
- [x] step1 Closed 完成后，只提示继续 step2。
- [x] step2 Fist 完成后，只提示继续 step3。
- [x] step3 Spread 完成后，只提示继续 step4 Crosstalk，不输出角度。
- [x] step4 Crosstalk 完成后，才提示“四步校准完成，开始实时输出角度”。
- [x] 按 `C` 重置后，四步校准状态全部清空，必须重新从 step1 开始。

### 4. 编译验收

至少执行：

```powershell
cmd /c """D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c hand_algorithm.cpp /FoNUL"
```

通过标准：

- 命令退出码为 0。
- 不出现编译错误。

建议同时执行：

```powershell
cmd /c """D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c main.cpp /FoNUL"
```

通过标准：

- 如环境依赖完整，命令退出码为 0。
- 如果由于本机串口或 Windows SDK 依赖导致无法单文件编译，agent 必须记录实际错误，不能直接写通过。

## 五、给执行 agent 的要求

1. 只做本文件列出的主体项目修正，不处理打包、发布、CMakeLists.txt 或 SDK 发布形态。
2. 不要新增兼容性方案，不要保留三步 ready 的旧语义。
3. 修改完成后，在“三、修改任务清单”和“四、全链路验收标准”对应条目后打勾。
4. 打勾后，把实际执行过的编译命令和结果写在本文件末尾。
5. 修改完成后交回审查，审查重点是：四步校准是否真正统一进入算法层 `isReady()`。

---

## 六、执行记录

### 编译命令

```powershell
# 算法编译
cmd /c "call VSDevCmd.bat -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c hand_algorithm.cpp /FoNUL"
→ 退出码 0

# 主流程编译
cmd /c "call VSDevCmd.bat -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c main.cpp /FoNUL"
→ 退出码 0

# SDK wrapper 编译
cmd /c "call VSDevCmd.bat -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c matrix_hand_sdk.cpp /FoNUL"
→ 退出码 0
```

### 静态验收

```powershell
# hasCrosstalkCalibration 已删除
rg -n "hasCrosstalkCalibration" main.cpp
→ 无输出

# isReady 包含 hasXtalk_
rg -n "bool HandAngleAlgorithm::isReady|hasXtalk_|hasBaseCalibration" hand_algorithm.cpp hand_algorithm.h
→ hand_algorithm.h:66:    bool hasBaseCalibration() const;
→ hand_algorithm.h:68:    bool isReady() const;
→ hand_algorithm.cpp:320:  bool HandAngleAlgorithm::hasBaseCalibration() const {
→ hand_algorithm.cpp:324:  bool HandAngleAlgorithm::isReady() const {
→ hand_algorithm.cpp:325:      return hasBaseCalibration() && hasXtalk_;   # ← 四步 ready

# finishCalibration Crosstalk 前置使用 hasBaseCalibration
→ hand_algorithm.cpp:285:      if (!hasBaseCalibration()) {

# main.cpp 输出门槛只使用 algorithm.isReady()
→ main.cpp:277:            !algorithm.isReady()) {
```

### 修改文件清单

| 文件 | 操作 |
|------|------|
| `hand_algorithm.h` | +`hasBaseCalibration()` 声明，`isReady()` 注释改为四步 |
| `hand_algorithm.cpp` | +`hasBaseCalibration()`，`isReady()` → `hasBaseCalibration() && hasXtalk_`，Crosstalk 检查改用 `hasBaseCalibration()` |
| `main.cpp` | -`hasCrosstalkCalibration` 变量，-参数，-赋值，输出门槛只用 `algorithm.isReady()` |
| `README.md` | 校准流程删"可选"，设计约束改"必须四步"，`is_ready` 改"四步" |
| `docs/api.md` | `is_ready` 改"四步"，Crosstalk 改"必须" |
| `docs/calibration.md` | `is_ready` 注释移到 Crosstalk 完成后 |

---

## 七、本轮审查记录

### 审查结论

**结论：暂不完全通过。**

主链路已经修正：`isReady()` 已经变成四步 ready，`finishCalibration()` 中 Crosstalk 前置检查已经改为基础校准判断，`main.cpp` 也不再维护 `hasCrosstalkCalibration` 第二套状态。

但还有 1 个需要调整的点：

- [x] `HandAngleAlgorithm::hasBaseCalibration()` 现在放在 `public` 区域，应移动到 `private` 区域。

原因：本函数只是为了让 Crosstalk 阶段内部检查 Closed/Fist/Spread 是否完成。当前将它暴露为 public，会重新给外部调用方一个“三步基础校准状态”的入口，容易和“四步完成后才 ready”的主语义混用。

目标：

```cpp
class HandAngleAlgorithm {
public:
    ...
    bool isReady() const;
    ...

private:
    bool hasBaseCalibration() const;
    ...
};
```

### 已复核通过项

- [x] `hand_algorithm.cpp` 中 `isReady()` 当前为：

```cpp
return hasBaseCalibration() && hasXtalk_;
```

- [x] `finishCalibration()` 的 Crosstalk 分支当前使用：

```cpp
if (!hasBaseCalibration()) {
    resetSamplingState();
    return false;
}
```

- [x] `processFrame()` 仍通过 `isReady()` 统一控制未 ready 输出，并在未 ready 时清零输出。
- [x] `main.cpp` 中未再检索到 `hasCrosstalkCalibration`。
- [x] `main.cpp` 实时输出门槛只依赖 `algorithm.isReady()`。
- [x] README、`docs/api.md`、`docs/calibration.md` 中已经去掉 “Crosstalk 可选 / 三步即可输出 / 三步即可 ready” 的错误语义。

### 本轮实际执行命令

```powershell
rg -n "hasCrosstalkCalibration" main.cpp
```

结果：无输出。

```powershell
rg -n "hasBaseCalibration\(" .
```

结果：仅命中 `hand_algorithm.h`、`hand_algorithm.cpp` 和本文件。代码中未发现外部业务调用，但声明位置仍需要改为 `private`。

```powershell
cmd /c "`"D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c hand_algorithm.cpp /FoNUL"
```

结果：退出码 0。

```powershell
cmd /c "`"D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c main.cpp /FoNUL"
```

结果：退出码 0。

### 交给执行 agent 的下一步

只需要把 `hasBaseCalibration()` 声明从 `public` 移到 `private`，不要改业务逻辑。改完后在本节未完成项打勾，再重新执行 `hand_algorithm.cpp` 和 `main.cpp` 编译验收。

---

## 八、最终复审记录

### 审查结论

**结论：通过。**

上一轮唯一未通过项已经完成：`HandAngleAlgorithm::hasBaseCalibration()` 已从 `public` 移到 `private`，不再对外暴露“三步基础校准状态”。

当前四步校准主链路成立：

- [x] `isReady()` 统一表示 Closed/Fist/Spread/Crosstalk 四步全部完成。
- [x] Crosstalk 校准前置检查使用内部 `hasBaseCalibration()`，不会和四步 `isReady()` 互相卡死。
- [x] `processFrame()` 继续只通过 `isReady()` 控制实时输出，未 ready 时清零输出。
- [x] `main.cpp` 不再维护 `hasCrosstalkCalibration` 第二套状态。
- [x] `main.cpp` 输出门槛只依赖 `algorithm.isReady()`。
- [x] 文档中未再发现 “Crosstalk 可选 / 三步校准即可输出 / 查询三步 ready” 的旧语义。

### 本轮复审命令

```powershell
rg -n "hasBaseCalibration\(" README.md docs hand_algorithm.h hand_algorithm.cpp main.cpp sdk优化.md
```

结果：代码中 `hand_algorithm.h` 的声明位于 `private` 区域，`hand_algorithm.cpp` 内部调用正常。

```powershell
rg -n "hasCrosstalkCalibration" main.cpp hand_algorithm.cpp hand_algorithm.h README.md docs
```

结果：无输出。

```powershell
rg -n "Crosstalk.*可选|串扰.*可选|三步校准.*输出|三步校准.*ready|三步.*返回 true|查询三步|前三步完成后即可输出|非必须" README.md docs hand_algorithm.h main.cpp hand_algorithm.cpp
```

结果：无输出。

```powershell
cmd /c "`"D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c hand_algorithm.cpp /FoNUL"
```

结果：退出码 0。

```powershell
cmd /c "`"D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c main.cpp /FoNUL"
```

结果：退出码 0。

### 最终判断

主体项目四步校准逻辑已经统一到算法层 `isReady()`，串扰补偿作为第四个必须校准动作已经正确参与 ready 状态。当前没有需要继续修改的主体校准问题。

---

## 九、删除 C 风格导出层记录

### 处理结论

当前阶段先完成主体项目，暂不需要导出层，已删除 C 风格 SDK 导出文件和对应 C API 文档。

- [x] 删除 `matrix_hand_sdk.h`。
- [x] 删除 `matrix_hand_sdk.cpp`。
- [x] 删除 `docs/api.md`。
- [x] `README.md` 删除 C 风格公开 API 表述，改为主体项目说明。
- [x] `docs/calibration.md` 删除 `matrix_hand_*` 调用示例，改为 `HandAngleAlgorithm` C++ 调用示例。

### 验收要求

```powershell
rg -n "matrix_hand_sdk|MatrixHandHandle|MATRIX_HAND_|matrix_hand_" . --glob "!opencv/**" --glob "!build/**" --glob "!x64/**"
```

通过标准：

- 除本文件历史记录外，不应再出现 C 风格导出接口引用。

```powershell
cmd /c "`"D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c hand_algorithm.cpp /FoNUL"
cmd /c "`"D:\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat`" -arch=x64 >nul && cl /nologo /std:c++17 /EHsc /utf-8 /c main.cpp /FoNUL"
```

通过标准：

- 两条命令退出码均为 0。
