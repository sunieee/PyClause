# 排序函数分析：sortAndProcessNoisy 和 sortAndProcessMax

## 概述

这两个函数用于对候选实体(candidate)进行排序,但采用了完全不同的数学原理和排序策略。

---

## 1. sortAndProcessNoisy (假设没有 applyComboAdjustmentsNoisyor)

### 1.1 排序依据

**排序依据**: 聚合的 surprisal (aggregated surprisal) **降序排序**

代码位置: `Application.cpp:722-747`

```cpp
// Sort by aggregated surprisal (higher is better - lower failure probability)
std::sort(
    candScoresToSort.begin(),
    candScoresToSort.end(), 
    [](const std::pair<int, double>& a, const std::pair<int, double>& b) {
        return a.second > b.second;  // 降序: surprisal 越大越好
    }
);
```

### 1.2 数学原理

#### 1.2.1 Noisy-OR 模型

Noisy-OR 是基于独立规则假设的概率聚合模型。假设规则 `r_1, r_2, ..., r_n` 独立地预测候选实体 `c`，每个规则的置信度为 `p_i`。

#### 1.2.2 Surprisal 的定义

对于每个规则 `i`，其 **surprisal** 定义为：

```
surprisal_i = -log(1 - p_i)
```

其中 `p_i` 是规则的置信度 (confidence)。

**代码验证**: `Rule.cpp:49-51`
```cpp
double Rule::getSurprisal(bool exact){
    double confidence = getConfidence(exact);
    return -std::log(1.0 - confidence);  // surprisal = -log(1-p)
}
```

#### 1.2.3 聚合 Surprisal 的计算

对于候选实体 `c`，其**聚合 surprisal** 是应用到该候选的所有规则的 surprisal 之和：

```
aggregated_surprisal(c) = Σ_{i: rule_i applies to c} surprisal_i
                        = Σ_{i} (-log(1 - p_i))
```

**代码验证**: `QueryResults.cpp:52-54`
```cpp
if (performAggregation && aggregationFunction=="noisyor" ){
    candScores[cand] += rule->getSurprisal();  // 累加所有规则的 surprisal
}
```

#### 1.2.4 最终概率的计算

根据 Noisy-OR 模型，候选实体 `c` 的**最终预测概率**为：

```
P(c) = 1 - Π_{i} (1 - p_i)
     = 1 - exp(-Σ_{i} (-log(1 - p_i)))
     = 1 - exp(-aggregated_surprisal(c))
```

**代码验证**: `Application.cpp:749-753`
```cpp
// Convert from aggregated surprisal to final probability: 1 - exp(-aggregatedSurprisal)
for (auto& pair: candScoresToSort){
    double aggregatedSurprisal = pair.second;
    pair.second = 1.0 - std::exp(-aggregatedSurprisal);  // P = 1 - exp(-Σ surprisal)
}
```

#### 1.2.5 排序的数学依据

- **为什么按 surprisal 降序排序？**
  
  因为 `aggregated_surprisal` 是单调递增函数：
  
  ```
  P(c) = 1 - exp(-aggregated_surprisal(c))
  ```
  
  当 `aggregated_surprisal` 增大时，`exp(-aggregated_surprisal)` 减小，因此 `P(c)` 增大。
  
  因此，**按聚合 surprisal 降序排序等价于按最终概率降序排序**。
  
- **等价性证明**:
  
  对于两个候选 `c_1` 和 `c_2`:
  
  ```
  aggregated_surprisal(c_1) > aggregated_surprisal(c_2)
  ⟺ exp(-aggregated_surprisal(c_1)) < exp(-aggregated_surprisal(c_2))
  ⟺ 1 - exp(-aggregated_surprisal(c_1)) > 1 - exp(-aggregated_surprisal(c_2))
  ⟺ P(c_1) > P(c_2)
  ```

### 1.3 排序流程总结

1. **收集阶段**: 在 `QueryResults` 中累加每个候选的所有规则 surprisal
2. **(可选)调整阶段**: 如果没有 `applyComboAdjustmentsNoisyor`，跳过此步
3. **排序阶段**: 按聚合 surprisal **降序**排序 (第710行后)
4. **转换阶段**: 将 surprisal 转换为概率 `1 - exp(-surprisal)` (第749-753行)

### 1.4 平局处理 (Tie Handling)

当两个候选的 `aggregated_surprisal` 相同时：

- **random**: 随机顺序
- **frequency**: 按实体频率降序，频率相同则按实体 ID 升序

**代码**: `Application.cpp:731-744`

---

## 2. sortAndProcessMax

### 2.1 排序依据

**排序依据**: 规则的置信度 (confidence) **字典序排序**

代码位置: `Application.cpp:941-975` (在 `scoreMaxPlus` 函数中)

### 2.2 排序规则

对候选实体 `c`，按其应用的**规则列表**进行**字典序**比较：

1. **主要比较**: 按规则置信度从高到低逐条比较
   ```cpp
   for (int i=0; i<minRules; i++){
       double confA = rulesA[i]->getConfidence();
       double confB = rulesB[i]->getConfidence();
       if (confA > confB) return true;   // A 排在 B 前面
       else if (confB > confA) return false;  // B 排在 A 前面
   }
   ```

2. **次要比较**: 如果所有规则置信度都相同，则规则数量多的排在前面
   ```cpp
   if (rulesB.size() > rulesA.size()) return false;
   else if (rulesA.size() > rulesB.size()) return true;
   ```

3. **平局处理**: 如果规则完全相同
   - **random**: 随机顺序
   - **frequency**: 按实体频率降序，频率相同则按实体 ID 升序

### 2.3 数学原理

#### 2.3.1 Max-Plus 模型

Max-Plus 是一种**非概率的确定性排序方法**，其核心思想是：

```
score(c) = max_{i: rule_i applies to c} confidence_i
```

即，候选实体 `c` 的分数等于应用到该候选的**所有规则中置信度最高的规则**的置信度。

**代码验证**: `Application.cpp:978-986`
```cpp
// take sorted candidate and derive its score according to highest rule
for (auto& pair: candsToSort){
    aggrCand.push_back(
        std::make_pair(
            pair.first,
            pair.second[0]->getConfidence()  // 取第一个规则(置信度最高)的置信度
        )
    );
}
```

#### 2.3.2 字典序排序的数学依据

虽然最终分数是 `max(confidence)`，但排序使用的是**字典序**，这确保了：

1. **确定性**: 如果两个候选的最高置信度相同，可以通过规则列表的完整比较来打破平局
2. **稳定性**: 相同规则集的候选总是得到相同的排序

#### 2.3.3 排序流程

```
候选 c₁: rules = [r₁(0.9), r₂(0.8), r₃(0.7)]
候选 c₂: rules = [r₁(0.9), r₂(0.7), r₄(0.6)]
```

比较过程：
1. 比较第 1 个规则: `0.9 == 0.9` → 继续
2. 比较第 2 个规则: `0.8 > 0.7` → `c₁` 排在 `c₂` 前面

因此 `c₁` 的最终分数是 `0.9` (与 `c₂` 相同)，但由于字典序，`c₁` 排在前面。

### 2.4 Max-Plus vs Noisy-OR 的差异

| 特性 | Noisy-OR | Max-Plus |
|------|----------|----------|
| **聚合方式** | 求和 (surprisal 相加) | 取最大值 |
| **概率模型** | 基于独立假设的概率模型 | 确定性排序 |
| **数学公式** | `P = 1 - exp(-Σ surprisal)` | `score = max(confidence)` |
| **排序依据** | 聚合 surprisal | 规则置信度字典序 |
| **规则交互** | 考虑所有规则的综合效应 | 只考虑最高置信度规则 |
| **适用场景** | 需要概率解释的场景 | 需要确定性排序的场景 |

---

## 3. 总结

### 3.1 sortAndProcessNoisy (无 combo 调整)

- **排序依据**: 聚合 surprisal 降序
- **数学原理**: Noisy-OR 概率模型
- **公式**: `P(c) = 1 - exp(-Σ_i (-log(1-p_i)))`
- **特点**: 
  - 考虑所有规则的贡献
  - 规则越多且置信度越高，概率越大
  - 基于独立规则假设

### 3.2 sortAndProcessMax

- **排序依据**: 规则置信度字典序
- **数学原理**: Max-Plus 确定性排序
- **公式**: `score(c) = max_i(confidence_i)`
- **特点**:
  - 只考虑最高置信度规则
  - 字典序确保确定性排序
  - 不涉及概率计算

### 3.3 关键区别

1. **Noisy-OR**: 多条规则可以**协同增强**预测概率
2. **Max-Plus**: 只关注**最强的单一证据**，不进行协同计算

这两个方法的选择取决于应用场景：如果需要概率解释和规则协同效应，选择 Noisy-OR；如果需要简单、确定性的排序，选择 Max-Plus。
