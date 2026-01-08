# Triple Classification with Combo Rules Guide

## Overview

Triple classification uses the same combo and clustering strategies as link prediction, but applies them to scoring specific triples rather than ranking candidates.

## Parameter Configuration

### For Triple Classification (PredictionHandler)

Use the `prediction_handler` namespace:

```python
from clause import Options

options = Options()

# Basic aggregation function
options.set("prediction_handler.aggregation_function", "noisyor")  # or "maxplus"

# Combo strategy for noisyor
# Options: "none", "max", "greed", "all"
options.set("prediction_handler.combo_noisyor_method", "max")

# Rule clustering by body similarity
# Set to >1 to disable clustering
options.set("prediction_handler.min_rule_jaccard", 0.9)

# Number of top rules to apply per triple
options.set("prediction_handler.num_top_rules", -1)  # -1 for all rules

# Debug output (first N triples from thread 0)
options.set("prediction_handler.queryTopK", 10)

# Number of threads
options.set("prediction_handler.num_threads", -1)  # -1 for all available
```

### For Link Prediction (RankingHandler)

Use the `ranking_handler` namespace:

```python
# Same parameters but under ranking_handler
options.set("ranking_handler.aggregation_function", "noisyor")
options.set("ranking_handler.combo_noisyor_method", "max")
options.set("ranking_handler.min_rule_jaccard", 0.9)
options.set("ranking_handler.queryTopK", 100)
```

## Combo Methods Explained

### 1. `"none"` (Default)
- No combo rules applied
- Only individual rules contribute to scores

### 2. `"max"` (Recommended)
- Finds the combo with maximum surprisal lift
- Adds only that combo to the aggregated score
- Conservative approach, good for most cases

**For noisyor:**
```
aggregated_surprisal = sum(rule_surprisals) + max_combo_lift
final_score = 1 - exp(-aggregated_surprisal)
```

**For maxplus:**
```
score_list = [rule_confidences..., best_combo_confidence]
final_score = max(score_list)  # lexicographic comparison
```

### 3. `"greed"` (Balanced)
- Greedily selects combos by surprisal lift
- Ensures no rule is used in multiple combos
- More aggressive than "max"

```
aggregated_surprisal = sum(rule_surprisals) + sum(selected_combo_lifts)
```

### 4. `"all"` (Most Aggressive)
- Adds all fulfilled combo lifts
- Can include negative lifts
- Most aggressive scoring

```
aggregated_surprisal = sum(rule_surprisals) + sum(all_combo_lifts)
```

## Rule Clustering

The `min_rule_jaccard` parameter controls rule clustering based on body similarity:

```python
# Enable clustering (default)
options.set("prediction_handler.min_rule_jaccard", 0.9)

# Disable clustering
options.set("prediction_handler.min_rule_jaccard", 1.1)
```

**How it works:**
1. Rules with Jaccard similarity ≥ threshold form connected components
2. Within each component, only the rule with highest surprisal is used
3. Reduces redundancy from similar rules

## Complete Example

```python
from clause import Loader, Options
from clause.util.utils import load_triples
import c_clause

# Setup options
options = Options()

# Triple classification settings
options.set("prediction_handler.aggregation_function", "noisyor")
options.set("prediction_handler.combo_noisyor_method", "max")
options.set("prediction_handler.min_rule_jaccard", 0.9)
options.set("prediction_handler.num_top_rules", -1)
options.set("prediction_handler.queryTopK", 10)  # Debug first 10 triples
options.set("prediction_handler.num_threads", 4)

# Load data and rules
loader = c_clause.Loader(options.get("loader"))
loader.load_data(data="data/train.txt")
loader.load_rules("rules.txt")

# Also load combos if available
loader.load_combos("combos.txt")

# Score triples
pred_handler = c_clause.PredictionHandler(options.get("prediction_handler"))
test_triples = load_triples("data/test.txt", loader.get_index())

pred_handler.calculate_scores(
    test_triples,
    loader.get_train(),
    loader.get_rules()
)

scores = pred_handler.get_scores()
print(f"Scored {len(scores)} triples")
```

## Debug Output

When `queryTopK > 0`, the first N triples (from thread 0) will output:

**For noisyor:**
- Applied rules and their surprisals
- Clustering information (components with >1 rule)
- Combo selection (which combo chosen, lift value)
- Score before/after clustering and combo

**For maxplus:**
- Applied rules and confidences
- Combo found (if any)
- Whether combo changed max confidence
- Best confidence source (rule vs combo)

## Performance Tips

1. **For speed:**
   - Set `combo_noisyor_method = "none"` if combos not critical
   - Set `min_rule_jaccard = 1.1` to disable clustering
   - Reduce `num_top_rules` to stop early

2. **For accuracy:**
   - Use `combo_noisyor_method = "max"` or `"greed"`
   - Enable clustering with `min_rule_jaccard = 0.9`
   - Set `num_top_rules = -1` to apply all rules

3. **For debugging:**
   - Set `queryTopK = 100` to see detailed output
   - Use `num_threads = 1` for reproducible debug output

## Differences from Link Prediction

| Aspect | Link Prediction | Triple Classification |
|--------|----------------|----------------------|
| Task | Rank candidates | Score specific triples |
| Candidates | Many per query | One per triple |
| Namespace | `ranking_handler` | `prediction_handler` |
| Output | Ranked list | Scores per triple |
| Efficiency | O(R × C) | O(R) |

## Common Issues

### Issue: Parameters not taking effect

**Problem:** Setting parameters has no effect on scoring.

**Solution:** Make sure you're using the correct namespace:
```python
# WRONG for triple classification
options.set("ranking_handler.combo_noisyor_method", "max")

# CORRECT for triple classification
options.set("prediction_handler.combo_noisyor_method", "max")
```

### Issue: No debug output

**Problem:** No debug information printed.

**Solution:** 
1. Set `queryTopK > 0`
2. Ensure combos are loaded: `loader.load_combos()`
3. Check that combo_noisyor_method != "none"

### Issue: Compilation errors

**Problem:** C++ compilation fails after changes.

**Solution:** Clean and rebuild:
```bash
python setup.py clean --all
python setup.py build_ext --inplace
```
