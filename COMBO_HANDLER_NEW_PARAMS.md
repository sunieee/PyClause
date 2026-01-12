# 新增 Combo_Handler 超参数使用说明

## 概述

为PyClause的`combo_handler`添加了7个新的超参数，用于链接预测和三元组分类任务。这些参数可以通过配置文件或命令行参数进行设置。

## 新增超参数

### 1. **λ_h (head_unary_weight)** - Head变量Unary规则权重
- **参数名**: `head_unary_weight`
- **默认值**: 1.0
- **可选值**: 0.5, 1, 2
- **说明**: 控制预测head实体的unary规则的影响权重

### 2. **λ_t (tail_unary_weight)** - Tail变量Unary规则权重  
- **参数名**: `tail_unary_weight`
- **默认值**: 1.0
- **可选值**: 0.5, 1, 2
- **说明**: 控制预测tail实体的unary规则的影响权重

### 3. **τ (aggregate_sharpness)** - 聚合尖锐度
- **参数名**: `aggregate_sharpness`
- **默认值**: 1.0
- **可选值**: 1, 1.5, 2, 3
- **说明**: 控制noisyor和maxplus之间的过渡，值越高越接近max聚合

### 4. **β (negative_weight)** - 负边抑制强度
- **参数名**: `negative_weight`
- **默认值**: 1.0
- **可选值**: 0, 0.5, 1, 2
- **说明**: 控制冲突规则的抑制强度。通过加权入度实现：surprisal' = surprisal / (1 + β * d_i-)，其中d_i-是规则作为较弱规则的次数

### 5. **ρ (positive_weight)** - 正边协同强度
- **参数名**: `positive_weight`
- **默认值**: 1.0
- **可选值**: 0, 0.5, 1, 2
- **说明**: 控制协同规则之间的增强强度。最终协同贡献 = ρ * sum(selected_positive_lifts)

### 6. **positive_method** - 正边选择方法
- **参数名**: `positive_method`
- **默认值**: "matching1"
- **可选值**: "none", "mst", "matching1", "matching2", "all"
- **说明**: 
  - `none`: 不使用正边协同
  - `mst`: 最大生成树（允许树结构）
  - `matching1`: 最大匹配（b=1，每个规则最多连接1个其他规则）
  - `matching2`: b-匹配（b=2，每个规则最多连接2个其他规则）
  - `all`: 使用所有正边

### 7. **if_grouping** - 是否分组
- **参数名**: `if_grouping`
- **默认值**: true
- **可选值**: true, false
- **说明**: 是否根据combo关系对规则进行分组

## 使用方法

### 1. 通过配置文件 (config.yaml)

```yaml
loader:
  combo_handler:
    aggregation_function: "noisyor"
    head_unary_weight: 1.0
    tail_unary_weight: 1.0
    aggregate_sharpness: 1.0
    negative_weight: 1.0
    positive_weight: 1.0
    positive_method: "matching1"
    if_grouping: true
```

### 2. 通过Python代码

```python
from clause.config.options import Options

opts = Options()

# 设置聚合函数
opts.set("loader.combo_handler.aggregation_function", "noisyor")

# 设置新的超参数
opts.set("loader.combo_handler.head_unary_weight", 1.0)
opts.set("loader.combo_handler.tail_unary_weight", 1.0)
opts.set("loader.combo_handler.aggregate_sharpness", 1.0)
opts.set("loader.combo_handler.negative_weight", 1.0)
opts.set("loader.combo_handler.positive_weight", 1.0)
opts.set("loader.combo_handler.positive_method", "matching1")
opts.set("loader.combo_handler.if_grouping", True)
```

### 3. 通过命令行 (demo-eval.py)

```bash
python demo-eval.py \
    --dataset wnrr \
    --aggregation_function noisyor \
    --head_unary_weight 1.0 \
    --tail_unary_weight 1.0 \
    --aggregate_sharpness 1.0 \
    --negative_weight 1.0 \
    --positive_weight 1.0 \
    --positive_method matching1 \
    --if_grouping
```

## 使用场景示例

### 场景1: 标准链接预测（Synergy + Matching1）
平衡的权重配置，使用matching方法选择正边

```bash
python demo-eval.py \
    --aggregation_function noisyor \
    --positive_method matching1 \
    --positive_weight 1.0 \
    --negative_weight 1.0 \
    --head_unary_weight 1.0 \
    --tail_unary_weight 1.0 \
    --aggregate_sharpness 1.0 \
    --if_grouping
```

### 场景2: MST强协同链接预测
更高的正边权重，使用MST获得更多连接

```bash
python demo-eval.py \
    --aggregation_function noisyor \
    --positive_method mst \
    --positive_weight 2.0 \
    --negative_weight 1.0 \
    --head_unary_weight 0.5 \
    --tail_unary_weight 2.0 \
    --aggregate_sharpness 1.0 \
    --if_grouping
```

### 场景3: 尖锐聚合三元组分类
更高的尖锐度和负权重，用于区分性评分

```bash
python demo-eval.py \
    --aggregation_function noisyor \
    --positive_method matching2 \
    --positive_weight 1.0 \
    --negative_weight 2.0 \
    --head_unary_weight 1.0 \
    --tail_unary_weight 1.0 \
    --aggregate_sharpness 2.0 \
    --if_grouping
```

### 场景4: 接近MaxPlus的行为
非常高的尖锐度，接近max聚合

```bash
python demo-eval.py \
    --aggregation_function noisyor \
    --positive_method matching1 \
    --positive_weight 0.5 \
    --negative_weight 0.5 \
    --aggregate_sharpness 3.0 \
    --if_grouping
```

## 参数调优建议

1. **aggregate_sharpness (τ)**:
   - 从1.0开始（标准noisyor）
   - 增加到1.5-2.0可以获得更尖锐的区分
   - 3.0接近maxplus行为

2. **positive_weight (ρ)** 和 **negative_weight (β)**:
   - 都设为1.0作为基线
   - 增加positive_weight以强化协同效应
   - 增加negative_weight以更强地抑制冲突

3. **head_unary_weight (λ_h)** 和 **tail_unary_weight (λ_t)**:
   - 通常保持为1.0
   - 如果任务更关注某个方向，可以调整相应权重

4. **positive_method**:
   - 从"matching1"开始（平衡）
   - "mst"用于更多协同连接
   - "matching2"介于两者之间
   - "all"使用所有正边（可能过拟合）

5. **if_grouping**:
   - 默认启用（true）以利用combo结构
   - 只有在不想使用combo特性时才禁用

## 示例脚本

- `demo-eval-with-new-params.sh`: Bash脚本示例
- `demo-eval-hyperparameters-example.py`: Python脚本示例
- `test_combo_hyperparameters.py`: 参数测试脚本

## 文件修改清单

1. **clause/config-default.yaml**: 添加了新参数的默认配置
2. **src/c_clause/api/ComboHandler.h**: 添加了C++实现的getter/setter
3. **examples/demo-eval.py**: 添加了命令行参数支持
4. **examples/demo-eval-with-new-params.sh**: 使用示例
5. **examples/demo-eval-hyperparameters-example.py**: Python使用示例
6. **test_combo_hyperparameters.py**: 测试脚本

## 注意事项

- 所有参数都有合理的默认值，可以直接使用
- 建议先用默认值测试，然后根据任务逐步调整
- 参数之间可能存在交互效应，需要系统性调优
- 确保编译C++代码后才能使用新参数
