# 规则应用流程分析：规则收集与候选实体发现

## 概述

本文档分析 PyClause 中对于查询 `(h, r, ?)` 或 `(?, r, t)` 的规则应用流程，重点关注：
1. **如何收集规则**：哪些规则被用于预测
2. **如何找到候选实体**：规则如何通过图遍历找到候选实体

---

## 一、规则收集阶段

### 1.1 查询处理入口

**位置**：`Application.cpp::calculateQueryResults()`

**流程**：
```cpp
// 第480行：根据查询的关系获取相关规则
auto& relRules = rules.getRelRules(rel);
```

**关键点**：
- 查询 `(source, rel, ?)` 或 `(?, rel, source)` 中，`rel` 是目标关系
- `RuleStorage::getRelRules(rel)` 返回所有能预测关系 `rel` 的规则集合
- 规则在 `RuleStorage` 中按目标关系（`targetRel`）索引存储

### 1.2 规则存储结构

**位置**：`RuleStorage.h` 和 `RuleStorage.cpp`

**数据结构**：
```cpp
// RuleStorage 内部维护：
std::unordered_map<int, std::set<Rule*, compareRule>> relToRules;
// 键：关系ID (relation ID)
// 值：能预测该关系的规则集合（按某种顺序排序）
```

**规则索引过程**（`RuleStorage.cpp:165`）：
```cpp
// 加载规则时，根据规则的目标关系进行索引
relToRules[rules_ptr[i]->getTargetRel()].insert(rules_ptr[i].get());
```

**规则排序**：
- 规则存储在 `std::set<Rule*, compareRule>` 中
- `compareRule` 定义了规则的比较/排序方式（通常按置信度或ID排序）

---

## 二、规则应用阶段

### 2.1 规则遍历循环

**位置**：`Application.cpp:499-520`

```cpp
for (Rule* rule : relRules){
    // 调用规则预测函数
    (rule->*predictHeadOrTail)(source, train, qResults, filter);
    
    // 检查停止条件
    currSize = qResults.size();
    if (rank_numPreselect>0 && currSize>=rank_numPreselect){
        break;  // 达到预设候选数量
    }
    if (currSize>=adapted_topk){
        if (rank_discAtLeast>0 && qResults.checkDiscrimination()){
            break;  // 达到区分度要求
        }
        if (score_numTopRules>0 && qResults.checkNumTopRules()){
            break;  // 达到规则数量要求
        }
    }
}
```

**关键点**：
- 按顺序遍历该关系的所有规则
- 每个规则调用 `predictTailQuery` 或 `predictHeadQuery`
- 有多个提前停止条件（候选数量、区分度、规则数量）

### 2.2 规则预测函数

#### 2.2.1 函数签名

**位置**：`Rule.h:71-75`

```cpp
virtual bool predictHeadQuery(
    int tail, TripleStorage& triples, QueryResults& headResults, ManySet filterSet=ManySet()
);
virtual bool predictTailQuery(
    int head, TripleStorage& triples, QueryResults& tailResults, ManySet filterSet=ManySet()
);
```

**参数说明**：
- `head/tail`：查询中已知的实体（如 `(h, r, ?)` 中的 `h`）
- `triples`：训练数据（用于图遍历）
- `headResults/tailResults`：存储预测结果的 `QueryResults` 对象
- `filterSet`：需要过滤的实体集合（训练集中的实体、额外过滤集等）

#### 2.2.2 规则类型与预测方式

不同规则类型有不同的预测实现：

##### **RuleB（二元规则）**

**示例规则**：`r(X,Y) :- b1(X,A), b2(A,Y)`

**预测流程**（`Rule.cpp:301-315`）：
```cpp
bool RuleB::predictTailQuery(int head, TripleStorage& triples, QueryResults& tailResults, ManySet filterSet){ 
    Nodes closingEntities;  // 存储找到的候选实体
    std::set<int> substitutions = {head};  // 已替换的变量集合
    
    // 从 head 开始，通过 DFS 搜索满足规则体的实体路径
    searchCurrGroundings(1, head, substitutions, triples, closingEntities, relations, directions);
    
    // 将找到的候选实体插入结果
    bool madePred = false;
    for (const int& cEnt: closingEntities){ 
        if (!filterSet.contains(cEnt)){  // 过滤掉训练集中的实体
            tailResults.insertRule(cEnt, this);  // 记录：候选实体 cEnt 由规则 this 预测
            madePred = true;
        }
    }
    return madePred;
}
```

**DFS 搜索过程**（`Rule.cpp:332-364`）：
```cpp
void RuleB::searchCurrGroundings(
    int currAtomIdx,      // 当前处理的规则体原子索引
    int currEntity,       // 当前实体
    std::set<int>& substitutions,  // 已替换的变量（避免重复）
    TripleStorage& triples,
    Nodes& closingEntities,  // 输出：找到的候选实体
    std::vector<int>& rels,  // 规则中的关系序列
    std::vector<bool>& dirs  // 关系方向序列
)
{
    int currRel = rels[currAtomIdx];
    int* begin;
    int length;
    
    // 根据方向获取邻居实体
    dirs[currAtomIdx-1] ? 
        triples.getTforHR(currEntity, currRel, begin, length) :  // 尾实体
        triples.getHforTR(currEntity, currRel, begin, length);   // 头实体
    
    if (currAtomIdx == rels.size()-1){
        // 最后一个原子：找到的实体就是候选实体
        for (int i=0; i<length; i++){
            int ent = begin[i];
            if (substitutions.find(ent)==substitutions.end()){  // 对象同一性约束
                closingEntities.insert(ent);
            }
        }
    }else{
        // 中间原子：继续递归搜索
        if (branchingFactor>0 && length>branchingFactor){
            return;  // 分支因子限制
        }
        for (int i=0; i<length; i++){
            int ent = begin[i];
            if (substitutions.find(ent)==substitutions.end()){
                substitutions.insert(ent);
                searchCurrGroundings(currAtomIdx+1, ent, substitutions, triples, closingEntities, rels, dirs);
                substitutions.erase(ent);  // 回溯
            }
        }
    }
}
```

**示例**：
- 规则：`r(X,Y) :- b1(X,A), b2(A,Y)`
- 查询：`(h, r, ?)`
- 过程：
  1. 从 `h` 开始，查找满足 `b1(h, A)` 的实体 `A`
  2. 对每个 `A`，查找满足 `b2(A, Y)` 的实体 `Y`
  3. 所有找到的 `Y` 就是候选实体

##### **RuleC（包含常量的规则）**

**示例规则**：`r(X,c) :- b1(X,A), b2(A,d)` 或 `r(c,Y) :- b1(d,A), b2(A,Y)`

**预测流程**（`Rule.cpp:522-557`）：
- 规则包含常量（如 `c`, `d`）
- 需要检查查询实体是否与规则中的常量匹配
- 通过类似的 DFS 搜索，但起点是常量实体

##### **RuleD、RuleZ、RuleXXc、RuleXXd**

- 这些规则类型有各自的预测逻辑
- 但核心思想相同：通过图遍历在训练数据中查找满足规则体的路径

---

## 三、候选实体收集阶段

### 3.1 QueryResults 数据结构

**位置**：`QueryResults.h` 和 `QueryResults.cpp`

**核心数据结构**：
```cpp
class QueryResults {
    NodeToPredRules candRules;  // std::unordered_map<int, std::vector<Rule*>>
    // 键：候选实体ID
    // 值：预测该候选实体的规则列表
    
    std::vector<int> candidateOrder;  // 候选实体出现的顺序
    
    std::unordered_map<int, double> candScores;  // 候选实体的聚合分数（surprisal）
};
```

### 3.2 规则插入过程

**位置**：`QueryResults.cpp:16-56`

```cpp
void QueryResults::insertRule(int cand, Rule* rule){
    // 检查是否达到 topK 限制
    bool onlyUpdate = (addTopK > 0 && candidateOrder.size()>= addTopK && 
                       rule != firstRule && currentRule!=rule);
    
    auto it = candRules.find(cand);
    bool newCand = (it==candRules.end());
    
    // 如果是新候选实体，添加到顺序列表
    if (newCand && !onlyUpdate){
        candidateOrder.push_back(cand);
        // ... 区分度跟踪逻辑 ...
    }
    
    // 检查规则数量限制
    if (num_top_rules>0 && !newCand && it->second.size() >= num_top_rules){
        return;  // 该候选实体已有足够规则
    }
    
    // 将规则添加到该候选实体的规则列表
    if (!onlyUpdate || !newCand){
        candRules[cand].push_back(rule);
        
        // 如果是 noisy-or 聚合，累加 surprisal
        if (performAggregation && aggregationFunction=="noisyor"){
            candScores[cand] += rule->getSurprisal();
        }
    }
}
```

**关键点**：
- **一个候选实体可以对应多个规则**：`candRules[cand]` 是一个规则列表
- **规则按应用顺序添加**：先应用的规则先加入列表
- **实时聚合**：对于 noisy-or，在插入规则时累加 surprisal

---

## 四、完整流程示例

### 示例：查询 `(Alice, knows, ?)`

假设有以下规则：
1. `knows(X,Y) :- friend(X,Y)` （RuleB，置信度 0.9）
2. `knows(X,Y) :- friend(X,A), friend(A,Y)` （RuleB，置信度 0.7）
3. `knows(X,Bob) :- worksAt(X,Company1), worksAt(Bob,Company1)` （RuleC，置信度 0.8）

**步骤 1：规则收集**
```cpp
relRules = rules.getRelRules(knows);  
// 返回：{rule1, rule2, rule3}（按某种顺序排序）
```

**步骤 2：规则应用**

**规则 1 应用**：
```cpp
rule1->predictTailQuery(Alice, train, qResults, filter);
// 在训练数据中查找 friend(Alice, Y)
// 找到：{Bob, Charlie}
// 执行：
//   qResults.insertRule(Bob, rule1);
//   qResults.insertRule(Charlie, rule1);
```

**规则 2 应用**：
```cpp
rule2->predictTailQuery(Alice, train, qResults, filter);
// 在训练数据中查找 friend(Alice, A), friend(A, Y)
// 找到：{David}
// 执行：
//   qResults.insertRule(David, rule2);
```

**规则 3 应用**：
```cpp
rule3->predictTailQuery(Alice, train, qResults, filter);
// 检查：Alice 和 Bob 是否都在 Company1 工作
// 如果满足，执行：
//   qResults.insertRule(Bob, rule3);
```

**步骤 3：结果收集**

最终 `qResults.candRules` 内容：
```cpp
{
    Bob: [rule1, rule3],      // Bob 被两个规则预测
    Charlie: [rule1],         // Charlie 被一个规则预测
    David: [rule2]            // David 被一个规则预测
}
```

**步骤 4：聚合与排序**

在 `sortAndProcessNoisy` 或 `sortAndProcessMax` 中：
- 对每个候选实体，聚合其所有规则的分数
- 根据聚合分数排序
- 输出最终排名

---

## 五、关键数据结构总结

### 5.1 规则到候选实体的映射

```
RuleStorage
  └── relToRules[relation]  →  std::set<Rule*>
       └── 每个 Rule
            └── predictTailQuery/predictHeadQuery
                 └── 找到候选实体
                      └── QueryResults::insertRule(candidate, rule)
```

### 5.2 候选实体到规则的映射

```
QueryResults
  └── candRules  →  std::unordered_map<int, std::vector<Rule*>>
       └── candidate_id  →  [rule1, rule2, rule3, ...]
            └── 规则按应用顺序存储
```

### 5.3 数据流

```
查询 (source, rel, ?)
    ↓
获取规则：relRules = rules.getRelRules(rel)
    ↓
遍历规则：for each rule in relRules
    ↓
规则预测：rule->predictTailQuery(source, train, qResults, filter)
    ↓
图遍历：在训练数据中查找满足规则体的路径
    ↓
找到候选实体：closingEntities
    ↓
插入结果：qResults.insertRule(candidate, rule)
    ↓
聚合与排序：sortAndProcessNoisy/Max
    ↓
最终排名：sortedCandScores
```

---

## 六、过滤机制

### 6.1 训练集过滤

**位置**：`Application.cpp:482-488`

```cpp
if (rank_filterWtrain){
    Nodes* trainFilter = nullptr;
    trainFilter = (!dirIsTail) ? 
        train.getHforTR(source, rel) :  // head 查询
        train.getTforHR(source, rel);    // tail 查询
    if (trainFilter){
        filter.addSet(trainFilter);  // 添加到过滤集
    }
}
```

**作用**：过滤掉训练集中已存在的三元组，避免数据泄露

### 6.2 额外过滤集

**位置**：`Application.cpp:490-495`

```cpp
Nodes* naddFilter = nullptr;
naddFilter = (!dirIsTail) ? 
    addFilter.getHforTR(source, rel) : 
    addFilter.getTforHR(source, rel);
if (naddFilter){
    filter.addSet(naddFilter);
}
```

**作用**：用户指定的额外过滤实体集合

### 6.3 规则预测中的过滤

在 `predictTailQuery` 中：
```cpp
if (!filterSet.contains(cEnt)){
    tailResults.insertRule(cEnt, this);
}
```

只有不在过滤集中的候选实体才会被添加。

---

## 七、停止条件

规则应用过程中有多个提前停止条件：

1. **预设候选数量**（`rank_numPreselect`）：
   ```cpp
   if (rank_numPreselect>0 && currSize>=rank_numPreselect){
       break;
   }
   ```

2. **区分度检查**（`rank_discAtLeast`）：
   ```cpp
   if (rank_discAtLeast>0 && qResults.checkDiscrimination()){
       break;
   }
   ```
   - 确保前 topK 个候选实体能被规则区分开

3. **规则数量限制**（`score_numTopRules`）：
   ```cpp
   if (score_numTopRules>0 && qResults.checkNumTopRules()){
       break;
   }
   ```
   - 确保前 topK 个候选实体都有足够数量的规则

---

## 八、总结

### 规则收集
- **来源**：`RuleStorage::getRelRules(rel)` 根据目标关系获取规则
- **顺序**：规则按 `compareRule` 定义的顺序（通常按置信度或ID）
- **类型**：包括 RuleB、RuleC、RuleD、RuleZ、RuleXXc、RuleXXd 等

### 候选实体发现
- **方法**：通过 DFS 图遍历在训练数据中查找满足规则体的路径
- **起点**：查询中的已知实体（head 或 tail）
- **约束**：对象同一性约束（变量不能绑定到相同实体）、分支因子限制

### 结果存储
- **结构**：`QueryResults::candRules` 是 `候选实体 → 规则列表` 的映射
- **特点**：一个候选实体可以对应多个规则，规则按应用顺序存储
- **聚合**：在插入规则时实时计算聚合分数（noisy-or 模式）

### 关键设计
1. **按关系索引规则**：高效获取相关规则
2. **图遍历预测**：利用知识图谱的邻接结构快速查找候选实体
3. **增量聚合**：边应用规则边计算分数，提高效率
4. **灵活过滤**：支持训练集过滤和额外过滤集
