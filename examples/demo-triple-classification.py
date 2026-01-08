from c_clause import PredictionHandler, Loader
from clause import Options
from sklearn.metrics import roc_auc_score, precision_recall_curve, average_precision_score
import numpy as np
import random
import os
import argparse


def load_triples(filepath):
    """Load triples from file."""
    triples = []
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) >= 3:
                triples.append(parts[:3])
    return triples


def generate_negatives(positives, known_triples, all_entities, seed=42):
    """Generate negative samples by corrupting positives, avoiding all known triples."""
    random.seed(seed)
    negatives = []
    
    print(f"  Generating {len(positives)} negative samples...")
    for h, r, t in positives:
        while True:
            if random.random() < 0.5:
                candidate = (random.choice(all_entities), r, t)
            else:
                candidate = (h, r, random.choice(all_entities))
            
            if candidate not in known_triples:
                negatives.append(list(candidate))
                break
    
    return negatives


def load_or_generate_negatives(base_dir, dataset, loader):
    """Load or generate test samples with labels."""
    data_dir = f"data/{dataset}"
    test_file = f"{data_dir}/test.txt"
    valid_file = f"{data_dir}/valid.txt"
    train_file = f"{data_dir}/train.txt"
    neg_file = f"{data_dir}/test_negatives.txt"
    
    # Load all known triples (train + valid + test)
    known_triples = set()
    for filepath in [train_file, valid_file, test_file]:
        if os.path.exists(filepath):
            for triple in load_triples(filepath):
                known_triples.add(tuple(triple))
    
    # Load positive test samples
    positives = load_triples(test_file)
    print(f"  Loaded {len(positives)} positive samples")
    
    # Load or generate negatives
    if os.path.exists(neg_file):
        negatives = load_triples(neg_file)
        print(f"  Loaded {len(negatives)} negative samples")
    else:
        print(f"  Generating negative samples...")
        all_entities = list(loader.get_entity_index())
        negatives = generate_negatives(positives, known_triples, all_entities)
        
        with open(neg_file, 'w', encoding='utf-8') as f:
            for triple in negatives:
                f.write('\t'.join(triple) + '\n')
        print(f"  Saved {len(negatives)} negative samples")
    
    return positives, negatives


def calculate_metrics(scores, labels):
    """Calculate classification metrics."""
    # AUC-ROC
    auc = roc_auc_score(labels, scores)
    
    # Precision-Recall curve and best F1
    precision, recall, thresholds = precision_recall_curve(labels, scores)
    f1_scores = 2 * (precision[:-1] * recall[:-1]) / (precision[:-1] + recall[:-1] + 1e-10)
    best_f1_idx = np.argmax(f1_scores)
    
    # Best accuracy
    best_acc = 0.0
    best_threshold_acc = 0.0
    for threshold in sorted(set(scores)):
        acc = ((scores >= threshold).astype(int) == labels).sum() / len(labels)
        if acc > best_acc:
            best_acc = acc
            best_threshold_acc = threshold
    
    return {
        'auc': auc,
        'ap': average_precision_score(labels, scores),
        'best_f1': f1_scores[best_f1_idx],
        'f1_threshold': thresholds[best_f1_idx],
        'f1_precision': precision[best_f1_idx],
        'f1_recall': recall[best_f1_idx],
        'best_acc': best_acc,
        'acc_threshold': best_threshold_acc
    }


# *** Argument Parser ***
argparser = argparse.ArgumentParser(description="Triple Classification using PyClause")
argparser.add_argument("--dataset", type=str, default="wnrr", help="dataset to use")
argparser.add_argument("--rules", type=str, default="", help="rules to use")
argparser.add_argument("--jaccard_file", type=str, default="", help="jaccard file to use")
argparser.add_argument("--aggregation_function", type=str, default="maxplus", help="aggregation function to use")
argparser.add_argument("--combo_noisyor_method", type=str, default="none", help="combo noisy or method to use")
argparser.add_argument("--disable_b", action="store_true", help="whether to disable b rules")
argparser.add_argument("--disable_combo", action="store_true", help="whether to disable combo rules")
argparser.add_argument("--disable_u_d", action="store_true", help="whether to disable u_d rules")
argparser.add_argument("--disable_u_c", action="store_true", help="whether to disable u_c rules")
argparser.add_argument("--disable_zero", action="store_true", help="whether to disable zero rules")
argparser.add_argument("--disable_u_xxc", action="store_true", help="whether to disable u_xxc rules")
argparser.add_argument("--disable_u_xxd", action="store_true", help="whether to disable u_xxd rules")
argparser.add_argument("--combo_debug", action="store_true", help="whether to enable combo debug")
argparser.add_argument("--combo_max_depth", type=int, default=-1, help="max depth for combo rules")
argparser.add_argument("--combo_max_branch", type=int, default=-1, help="max branch for combo rules")
argparser.add_argument("--b_max_length", type=int, default=-1, help="max length for b rules")
argparser.add_argument("--d_weight", type=float, default=0.1, help="weight for d rules")
argparser.add_argument("--z_weight", type=float, default=0.01, help="weight for zero rules")
argparser.add_argument("--num_top_rules", type=int, default=-1, help="number of top rules to use")

args = argparser.parse_args()
dataset = args.dataset
train = f"data/{dataset}/train.txt"
filter_set = f"data/{dataset}/valid.txt"
target = f"data/{dataset}/test.txt"

rules = args.rules if args.rules else f"data/rules/{dataset}.txt"

options = Options()
options.set("prediction_handler.aggregation_function", args.aggregation_function)
options.set("prediction_handler.num_top_rules", args.num_top_rules)
options.set("prediction_handler.combo_noisyor_method", args.combo_noisyor_method)
options.set("prediction_handler.queryTopK", 10)  # Enable debug output for first 10 triples
options.set("prediction_handler.min_rule_jaccard", 0.9)  # Enable rule clustering

options.set("loader.load_b_rules", not args.disable_b)
options.set("loader.load_combo", not args.disable_combo)
options.set("loader.load_zero_rules", not args.disable_zero)
options.set("loader.load_u_d_rules", not args.disable_u_d)
options.set("loader.load_u_c_rules", not args.disable_u_c)
options.set("loader.load_u_xxc_rules", not args.disable_u_xxc)
options.set("loader.load_u_xxd_rules", not args.disable_u_xxd)
options.set("loader.combo_debug", args.combo_debug)
options.set("loader.combo_max_depth", args.combo_max_depth)
options.set("loader.combo_max_branch", args.combo_max_branch)
options.set("loader.b_max_length", args.b_max_length)

# *** Set thread count ***
options.set("prediction_handler.num_threads", -1)
options.set("loader.num_threads", os.cpu_count())

base_dir = ""
dataset = args.dataset

print("\n" + "=" * 50)
print(f"Triple Classification - {dataset}")
print("=" * 50)

# Load data and rules
print("\n[1/4] Loading data and rules...")
loader = Loader(options=options.get("loader"))
loader.load_data(data=train, filter=filter_set, target=target)
loader.load_rules(rules=rules, jaccard=args.jaccard_file)
print(f"  Entities: {len(loader.get_entity_index())}, Relations: {len(loader.get_relation_index())}")

# Load test samples
print("\n[2/4] Loading test samples...")
positives, negatives = load_or_generate_negatives(base_dir, dataset, loader)
min_samples = min(len(positives), len(negatives))
all_triples = positives[:min_samples] + negatives[:min_samples]
labels = np.array([1] * min_samples + [0] * min_samples)
print(f"  Using {min_samples} positive and {min_samples} negative samples")

# Score triples
print("\n[3/4] Scoring triples...")
scorer = PredictionHandler(options=options.get("prediction_handler"))
scorer.calculate_scores(triples=all_triples, loader=loader)
scores = np.array([r[3] for r in scorer.get_scores(as_string=False)])
print(f"  Score range: [{scores.min():.4f}, {scores.max():.4f}], Mean: {scores.mean():.4f}")

# Calculate metrics
print("\n[4/4] Calculating metrics...")
metrics = calculate_metrics(scores, labels)

# Print results
print("\n" + "=" * 50)
print("RESULTS")
print("=" * 50)
print(f"AUC-ROC:           {metrics['auc']:.4f}")
print(f"Average Precision: {metrics['ap']:.4f}")
print(f"\nBest Accuracy:     {metrics['best_acc']:.4f}  (threshold: {metrics['acc_threshold']:.4f})")
print(f"\nBest F1:           {metrics['best_f1']:.4f}  (threshold: {metrics['f1_threshold']:.4f})")
print(f"  Precision:       {metrics['f1_precision']:.4f}")
print(f"  Recall:          {metrics['f1_recall']:.4f}")
print("=" * 50)

