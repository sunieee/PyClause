# 组合规则 (Combo Rules) 使用说明

## 概述

组合规则功能允许系统识别2-3个规则的组合，并使用组合的置信度进行预测。组合规则的置信度通常高于单个规则。

## 规则格式

组合规则使用分号(`;`)分隔多个规则体部，格式如下：

```
numPreds\tnumTrue\tconf\thead(X,Y) <= body1(X,A), body2(A,Y); body3(B,X), body4(B,Y)
```

### 示例

```
1500\t1200\t0.8\t/award/award_winner/awards_won./award/award_honor/award_winner(X,Y) <= /award/award_nominee/award_nominations./award/award_nomination/award_nominee(A,X), /award/award_winner/awards_won./award/award_honor/award_winner(B,A), /award/award_winner/awards_won./award/award_honor/award_winner(B,Y); /award/award_nominated_work/award_nominations./award/award_nomination/nominated_for(A,X), /award/award_nominated_work/award_nominations./award/award_nomination/nominated_for(B,A), /award/award_winner/awards_won./award/award_honor/award_winner(Y,B)
```

## 工作流程

### 1. 离线准备阶段（规则加载）

当加载规则文件时，系统会：

1. 检测规则字符串中是否包含分号`;`
2. 如果包含分号，调用`parseCombo()`函数
3. 将组合拆分为独立的子规则
4. 查找每个子规则对应的Rule对象
5. 创建Combo对象，存储：
   - `memberRules`: 排序后的子规则列表
   - `length`: 组合包含的规则数量(2或3)
   - `confidence`: 组合置信度
   - `hashCode`: 基于规则ID的哈希值
6. 构建倒排索引`ruleIDToCombos`: 从每个规则ID映射到包含它的所有组合

### 2. 查询应用阶段（预测）

在MaxPlus聚合策略下进行预测时：

1. 对于每个候选实体，获取其满足的规则列表
2. 调用`findMaxComboConfidence()`函数：
   - 初始化`combo2count`哈希表
   - 遍历应用的规则，累加每个组合的计数
   - 检查哪些组合的所有规则都被满足（计数等于组合长度）
   - 返回完全匹配组合中的最大置信度
3. 比较组合置信度与单规则最大置信度，取较大值

## 配置选项

### Python配置文件方式（推荐）

在`clause/config-default.yaml`或自定义配置文件中设置combo参数：

```yaml
loader:
  # 启用/禁用组合规则加载
  load_combo: True
  
  # 启用调试输出（显示解析和匹配过程）
  combo_debug: False
  
  # 最小预测数阈值（-1表示不过滤）
  combo_min_pred: -1
  
  # 最小支持度阈值，即正确预测数（-1表示不过滤）
  combo_min_support: -1
  
  # 最小置信度阈值
  combo_min_conf: 0.0001
```

使用自定义配置文件：

```python
from clause.config.options import Options
from c_clause import Loader

# 使用自定义配置
opts = Options(path='config-my.yaml')

# 或者程序中修改配置
opts = Options()
opts.set("loader.load_combo", True)
opts.set("loader.combo_debug", True)
opts.set("loader.combo_min_pred", 10)
opts.set("loader.combo_min_support", 5)
opts.set("loader.combo_min_conf", 0.001)

# 创建Loader时传入配置
loader = Loader(opts.get("loader"))
```

### C++直接配置方式

如果直接使用C++层，可以通过以下方式配置：

```cpp
// 启用/禁用组合规则
ruleFactory->setCreateCombo(true);  // 启用
ruleFactory->setCreateCombo(false); // 禁用

// 启用调试输出
ruleFactory->setComboDebug(true);

// 设置阈值（使用type="m"表示combo）
ruleFactory->setMinPred(10, "m");      // 最小预测数
ruleFactory->setMinCorrect(5, "m");    // 最小正确数（支持度）
ruleFactory->setMinConf(0.5, "m");     // 最小置信度
```

### 调试输出

当`combo_debug`设置为`True`时，系统会输出详细的调试信息：

#### 规则加载阶段
```
[RuleFactory] Detected semicolon - parsing as combo
[parseCombo] Head: relation(X,Y)
[parseCombo] Found 2 member bodies
[parseCombo] Processing member body 1: body1(X,A), body2(A,Y)
[parseCombo] Processing member body 2: body3(B,X), body4(B,Y)
[parseCombo] Creating combo with 2 members
[Combo] Constructor called with 2 rules
[parseCombo] Combo created successfully
```

#### 查询预测阶段
```
[findMaxComboConfidence] Checking 15 applied rules
[findMaxComboConfidence] Rule 42 in 3 combos
[findMaxComboConfidence] Rule 87 in 2 combos
...
[findMaxComboConfidence] Combo [42,87] complete with conf 0.85
[findMaxComboConfidence] Max combo conf: 0.85, single max: 0.60
```

这些输出帮助理解：
- 哪些规则被解析为组合
- 组合包含哪些子规则
- 查询时哪些组合被匹配
- 组合置信度与单规则置信度的比较

## 性能特性

- **时间复杂度**: O(n × k)，其中n是应用的规则数，k是平均每个规则属于的组合数
- **空间复杂度**: O(M × L)，其中M是组合总数，L是平均组合长度(2-3)
- **典型场景**: n=100, k=10, 查询时间约1000次哈希查找

## 注意事项

1. **规则必须预先存在**: 组合中的每个子规则必须作为单独的规则已经加载
2. **仅MaxPlus聚合**: 组合规则功能仅在MaxPlus聚合策略下生效
3. **自动排序**: 子规则会按照规则ID自动排序，确保一致性
4. **重复检测**: 系统会检测并拒绝重复的规则定义

## 示例场景

假设有以下规则：
```
R1: h(X,Y) <= b1(X,A), b2(A,Y)       [conf=0.6]
R2: h(X,Y) <= b3(X,B), b4(B,Y)       [conf=0.5]
R5: h(X,Y) <= b5(X,C), b6(C,Y)       [conf=0.7]
```

定义组合：
```
[R1, R2, R5]: 0.8
```

查询结果：
- 实体e1满足规则: [R1, R2, R5, R7, R9]
- 系统识别到R1, R2, R5组合完全匹配
- 输出置信度: 0.8 (而不是单规则最大值0.7)

## 错误处理

系统会在以下情况抛出异常：

1. 组合格式错误（没有分号、格式不正确）
2. 子规则数量不是2-3个
3. 找不到对应的子规则对象
4. 检测到重复规则定义

所有错误信息会包含详细的上下文，方便调试。
