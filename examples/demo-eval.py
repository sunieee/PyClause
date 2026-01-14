from c_clause import RankingHandler, Loader
from clause import Options

from clause import Ranking
from clause import TripleSet

import argparse
import os

# *** Example Evaluation ***

## This example illustrates how to create a ranking from a rule set that has been created previously.
## The ranking is evaluated on the fly before storing it on disc.
## The example shows also at the end how to use a few lines of code to create a
## structured results table that informs about relation and direction specific MRR and hits scores.

argparser = argparse.ArgumentParser(description="Example for evaluation of a ranking")
argparser.add_argument("--dataset", type=str, default="wnrr", help="dataset to use")
argparser.add_argument("--rules", type=str, default="", help="rules to use")
argparser.add_argument("--ranking_file", type=str, default="", help="rules to use")
argparser.add_argument("--aggregation_function", type=str, default="maxplus", help="aggregation function to use")
# New hyperparameters for link prediction and triple classification
argparser.add_argument("--binary_weight", type=float, default=1.0, help="λ: weight for binary rules (unary rules have fixed weight 1.0)")
argparser.add_argument("--aggregate_sharpness", type=float, default=1.0, help="τ: aggregate sharpness (noisyor↔maxplus)")
argparser.add_argument("--negative_weight", type=float, default=1.0, help="β: negative edge suppression strength")
argparser.add_argument("--positive_weight", type=float, default=1.0, help="ρ: positive edge synergy strength")
argparser.add_argument("--positive_method", type=str, default="matching1", help="positive method: mst, matching1, matching2, all")
argparser.add_argument("--no_grouping", action="store_true", help="disable grouping of rules")
argparser.add_argument("--disable_b", action="store_true", help="whether to disable b rules")
argparser.add_argument("--disable_combo", action="store_true", help="whether to disable combo rules")
argparser.add_argument("--disable_u_d", action="store_true", help="whether to disable u_d rules")
argparser.add_argument("--disable_u_c", action="store_true", help="whether to disable u_c rules")
argparser.add_argument("--disable_zero", action="store_true", help="whether to disable zero rules")
argparser.add_argument("--disable_u_xxc", action="store_true", help="whether to disable u_xxc rules")
argparser.add_argument("--disable_u_xxd", action="store_true", help="whether to disable u_xxd rules")
argparser.add_argument("--b_max_length", type=int, default=-1, help="whether to disable u_xxd rules")
argparser.add_argument("--num_unseen", type=int, default=5, help="whether to disable u_xxd rules")
argparser.add_argument("--d_weight", type=float, default=0.1, help="whether to disable u_xxd rules")
argparser.add_argument("--z_weight", type=float, default=0.01, help="whether to disable u_xxd rules")
argparser.add_argument("--test_valid_split", type=str, default="", help="whether to disable u_xxd rules")


args = argparser.parse_args()
dataset = args.dataset
train = f"data/{dataset}/train.txt"
filter_set = f"data/{dataset}/valid{args.test_valid_split}.txt"
target = f"data/{dataset}/test{args.test_valid_split}.txt"

# rules = f"{get_base_dir()}/data/rules/{dataset}.txt"
rules = args.rules if args.rules else f"data/rules/{dataset}.txt"
ranking_file = args.ranking_file if args.ranking_file else f"local/ranking-{dataset}.txt"

options = Options()
# options.set("ranking_handler.topk", args.topk)
# options.set("ranking_handler.combo_include_negative", args.combo_include_negative)
options.set("loader.load_b_rules", not args.disable_b)
options.set("loader.load_zero_rules", not args.disable_zero)
options.set("loader.load_u_d_rules", not args.disable_u_d)
options.set("loader.load_u_c_rules", not args.disable_u_c)
options.set("loader.load_u_xxc_rules", not args.disable_u_xxc)
options.set("loader.load_u_xxd_rules", not args.disable_u_xxd)
options.set("loader.b_max_length", args.b_max_length)
options.set("loader.num_unseen", args.num_unseen)

# ComboHandler 配置现在是 Loader 的一部分，使用 loader.combo_handler.* 路径
options.set("loader.combo_handler.aggregation_function", args.aggregation_function)
options.set("loader.combo_handler.query_topk", 100)

# Loader中的Combo规则加载配置（load_combo和combo_debug由RuleFactory使用）
options.set("loader.load_combo", not args.disable_combo)
options.set("loader.combo_debug", False)

# 新增超参数配置
options.set("loader.combo_handler.binary_weight", args.binary_weight)
options.set("loader.combo_handler.aggregate_sharpness", args.aggregate_sharpness)
options.set("loader.combo_handler.negative_weight", args.negative_weight)
options.set("loader.combo_handler.positive_weight", args.positive_weight)
options.set("loader.combo_handler.positive_method", args.positive_method)
options.set("loader.combo_handler.if_grouping", not args.no_grouping)


# *** 关键：设置线程数 ***
options.set("ranking_handler.num_threads", -1)  
options.set("loader.num_threads", os.cpu_count())           # 指定4个线程用于规则加载


#### Calculate a ranking
loader = Loader(options=options.get("loader"))
loader.load_data(data=train, filter=filter_set, target=target)
loader.load_rules(rules=rules)

# ComboHandler 配置现在由 Loader 管理，不再需要手动合并选项
# RankingHandler, QAHandler, PredictionHandler 都会从 Loader 获取相同的 combo 配置
ranker = RankingHandler(options=options.get("ranking_handler"))
ranker.calculate_ranking(loader=loader)
headRanking = ranker.get_ranking(direction="head", as_string=True)
tailRanking = ranker.get_ranking(direction="tail", as_string=True)

testset = TripleSet(target)
ranking = Ranking(k=100)

# process the handler ranking which is defined on queries and not
# on triples, e.g. assign to every triple of 'testset' the corresponding query rankings
ranking.convert_handler_ranking(headRanking, tailRanking, testset)
ranking.compute_scores(testset.triples)

print("*** EVALUATION RESULTS ****")
print("Num triples: " + str(len(testset.triples)))
print("MRR     " + '{0:.6f}'.format(ranking.hits.get_mrr()))
print("hits@1  " + '{0:.6f}'.format(ranking.hits.get_hits_at_k(1)))
print("hits@3  " + '{0:.6f}'.format(ranking.hits.get_hits_at_k(3)))
print("hits@10 " + '{0:.6f}'.format(ranking.hits.get_hits_at_k(10)))
print()

print("MRR " + '{0:.6f}'.format(ranking.hits.get_mrr()) + \
      ", hits@1 " + '{0:.6f}'.format(ranking.hits.get_hits_at_k(1)) + \
      ", hits@3 " + '{0:.6f}'.format(ranking.hits.get_hits_at_k(3)))
# now some code to some nice overview on the different relations and directions
# the loop interates over all relations in the test set
print("relation".ljust(25) + "\t" + "MRR-h" + "\t" + "MRR-t" + "\t" + "Num triples")
for rel in testset.rels:
   rel_token = testset.index.id2to[rel]
   # store all triples that use the current relation rel in rtriples
   rtriples = list(filter(lambda x: x.rel == rel, testset.triples))

   # compute scores in head direction ...
   ranking.compute_scores(rtriples, True, False)
   (mrr_head, h1_head) = (ranking.hits.get_mrr(), ranking.hits.get_hits_at_k(1))
   # ... and in tail direction
   ranking.compute_scores(rtriples, False, True)
   (mrr_tail, h1_tail) = (ranking.hits.get_mrr(), ranking.hits.get_hits_at_k(1))
   # print the resulting scores
   print(rel_token.ljust(25) +  "\t" + '{0:.3f}'.format(mrr_head) + "\t" + '{0:.3f}'.format(mrr_tail) + "\t" + str(len(rtriples)))


# finally, write the ranking to a file, there are two ways to to this, both reults into the same ranking
ranker.write_ranking(path=ranking_file, loader=loader)