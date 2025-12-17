# Combo Rules Configuration Summary

## 修改文件清单

### 1. 配置文件
- `clause/config-default.yaml`: 添加了5个combo配置项

### 2. C++核心代码
- `src/c_clause/core/RuleFactory.h`: 添加了`setComboDebug()`方法声明
- `src/c_clause/core/RuleFactory.cpp`: 
  - 实现`setComboDebug()`方法
  - 在`setMinCorrect()`, `setMinPred()`, `setMinConf()`中添加对"m"类型的支持

### 3. C++接口代码
- `src/c_clause/api/Loader.cpp`: 添加5个combo配置项的处理器

### 4. 示例和文档
- `examples/demo-combo-config.py`: 演示如何配置combo规则
- `COMBO_RULES_USAGE.md`: 更新配置章节，添加Python和C++两种配置方式

## 配置项说明

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `load_combo` | bool | True | 是否启用组合规则加载 |
| `combo_debug` | bool | False | 是否启用调试输出 |
| `combo_min_pred` | int | -1 | 最小预测数（-1表示不过滤） |
| `combo_min_support` | int | -1 | 最小支持度/正确数（-1表示不过滤） |
| `combo_min_conf` | float | 0.0001 | 最小置信度阈值 |

## 配置方式

### 方式1: YAML配置文件（推荐）

```yaml
# config-my.yaml
loader:
  load_combo: True
  combo_debug: True
  combo_min_pred: 10
  combo_min_support: 5
  combo_min_conf: 0.001
```

```python
from clause.config.options import Options
from c_clause import Loader

opts = Options(path='config-my.yaml')
loader = Loader(opts.get("loader"))
```

### 方式2: Python动态配置

```python
from clause.config.options import Options
from c_clause import Loader

opts = Options()
opts.set("loader.load_combo", True)
opts.set("loader.combo_debug", True)
opts.set("loader.combo_min_pred", 10)
opts.set("loader.combo_min_support", 5)
opts.set("loader.combo_min_conf", 0.001)

loader = Loader(opts.get("loader"))
```

### 方式3: C++直接调用（仅限C++项目）

```cpp
ruleFactory->setCreateCombo(true);
ruleFactory->setComboDebug(true);
ruleFactory->setMinPred(10, "m");
ruleFactory->setMinCorrect(5, "m");
ruleFactory->setMinConf(0.001, "m");
```

## 内部实现

### 配置传递流程

```
YAML文件 
  ↓
Options.flat("loader")
  ↓
Loader.setRuleOptions()
  ↓
配置处理器 (lambda函数)
  ↓
RuleFactory.setXxx() 方法
  ↓
RuleFactory 成员变量 / 全局变量
```

### 配置映射

| YAML配置 | C++方法调用 | 目标变量 |
|----------|------------|----------|
| `load_combo` | `setCreateCombo(bool)` | `RuleFactory::createCombo` |
| `combo_debug` | `setComboDebug(bool)` | `::comboDebug` (全局) |
| `combo_min_pred` | `setMinPred(int, "m")` | `RuleFactory::MminPreds` |
| `combo_min_support` | `setMinCorrect(int, "m")` | `RuleFactory::MminCorrect` |
| `combo_min_conf` | `setMinConf(double, "m")` | `RuleFactory::MminConf` |

## 测试验证

运行示例脚本验证配置：

```bash
cd /d C:\Users\sy650\IdeaProjects\PyClause
python examples/demo-combo-config.py
```

预期输出：
- 显示默认combo配置
- 显示修改后的配置
- 显示如何使用自定义配置文件

## 注意事项

1. **类型标识**: Combo规则使用"m"作为类型标识符（与b/c/d/z等规则类型一致）
2. **全局变量**: `comboDebug`是全局变量，定义在`Globals.h/cpp`中
3. **默认值**: 默认启用combo加载但禁用debug输出，避免过多日志
4. **阈值语义**: 
   - `combo_min_pred`: 对应numPreds（总预测数）
   - `combo_min_support`: 对应numTrue（正确预测数，又称support）
   - `combo_min_conf`: 对应confidence值

## 完整示例

```python
from clause.config.options import Options
from c_clause import Loader, QAHandler

# 1. 配置combo规则
opts = Options()
opts.set("loader.load_combo", True)
opts.set("loader.combo_debug", True)  # 开启调试
opts.set("loader.combo_min_conf", 0.001)

# 2. 创建Loader并加载数据和规则
loader = Loader(opts.get("loader"))
loader.load_data(
    data="data/wnrr/train.txt",
    target="data/wnrr/test.txt"
)

# 加载包含combo规则的文件
loader.load_rules("data/wnrr/combo-rules.txt")

# 3. 创建QA Handler进行预测
qa = QAHandler(opts.get("qa_handler"))
results = qa.answer_single(
    head_id=-1,
    rel_id=5,
    tail_id=100
)

# 查看结果和使用的规则
for candidate, score in results:
    print(f"Candidate: {candidate}, Score: {score}")
```

在上述示例中，如果`combo_debug=True`，控制台会输出：
- 规则加载时的combo解析过程
- 查询时combo匹配和置信度计算过程
