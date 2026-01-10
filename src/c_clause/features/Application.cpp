#include <map>
#include <omp.h>
#include <memory>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <functional>
#include <chrono>
#include <mutex>
#include <atomic>


#include "Application.h"
#include "../core/TripleStorage.h"
#include "../core/RuleStorage.h"
#include "../core/Types.h"
#include "../core/Rule.h"
#include "../core/Combo.h"
#include "../core/Globals.h"

// ================ Helper Functions for Modularized Strategies ================

void ApplicationHandler::debugOutputQueryInfo(
    int queryCount,
    int querySource,
    int queryRel,
    bool queryDirIsTail,
    const int* groundTruthTargets,
    int numGroundTruth,
    const std::unordered_map<int, std::vector<Rule*>>& candRules,
    RuleStorage& rules,
    TripleStorage& data
) {
    Index* index = data.getIndex();
    
    std::cout << "\n========== Query #" << queryCount << " Debug Info (Thread 0) ==========" << std::endl;
    std::cout << "  Query (IDs): " << querySource << " " << queryRel << " ?" << std::endl;
    std::cout << "  Query (Strings): \"" << index->getStringOfNodeId(querySource) << "\" \"" 
              << index->getStringOfRelId(queryRel) << "\" ?" << std::endl;
    std::cout << "  Direction: Predicting " << (queryDirIsTail ? "TAIL" : "HEAD") << std::endl;
    std::cout << "  Combo Positive Method: " << comboHandler.getNoisyorPositiveMethod() << std::endl;
    std::cout << "  Combo Negative Method: " << comboHandler.getNoisyorNegativeMethod() << std::endl;
    std::cout << "  Combo Lift Ratio: " << comboHandler.getLiftRatio() << std::endl;
    std::cout << "  Aggregation Function: " << comboHandler.getAggregationFunction() << std::endl;
    
    std::cout << "  Total Candidates: " << candRules.size() << std::endl;
    
    if (numGroundTruth > 0 && groundTruthTargets != nullptr) {
        std::cout << "  Ground Truth (" << numGroundTruth << " targets):" << std::endl;
        for (int i = 0; i < numGroundTruth; i++) {
            std::cout << "    - ID: " << groundTruthTargets[i] 
                      << ", String: \"" << index->getStringOfNodeId(groundTruthTargets[i]) << "\"" << std::endl;
        }
    }
}

void ApplicationHandler::applyComboAdjustmentsNoisyor(
    std::unordered_map<int, double>& aggregatedSurprisal,
    std::unordered_map<int, std::vector<Rule*>>& candRules,
    RuleStorage& rules,
    bool shouldDebug,
    TripleStorage& data,
    const int* groundTruthTargets,
    int numGroundTruth
) {
    // Check if we need to do anything
    bool hasNegativeMethod = (comboHandler.getNoisyorNegativeMethod() != "none");
    bool hasPositiveMethod = (comboHandler.getNoisyorPositiveMethod() != "none");
    
    if (!hasNegativeMethod && !hasPositiveMethod) {
        return;
    }
    
    if (!rules.hasCombos()) {
        return;
    }
    
    auto& ruleHashToCombos = rules.getRuleHashToCombos();
    
    // Track ranking changes for ground truth
    std::unordered_map<int, int> gtRanksBefore;
    std::unordered_map<int, int> gtRanksAfter;
    
    if (shouldDebug && groundTruthTargets != nullptr) {
        // Get ranking before combo
        std::vector<std::pair<int, double>> beforeRanking(aggregatedSurprisal.begin(), aggregatedSurprisal.end());
        std::sort(beforeRanking.begin(), beforeRanking.end(),
            [](const std::pair<int, double>& a, const std::pair<int, double>& b) {
                return a.second > b.second;
            });
        
        for (int i = 0; i < beforeRanking.size(); i++) {
            int candId = beforeRanking[i].first;
            for (int j = 0; j < numGroundTruth; j++) {
                if (candId == groundTruthTargets[j]) {
                    gtRanksBefore[candId] = i + 1;
                    break;
                }
            }
        }
    }
    
    // Process each candidate
    for (auto& candPair : candRules) {
        int candidate = candPair.first;
        std::vector<Rule*>& appliedRules = candPair.second;
        
        if (appliedRules.empty()) continue;
        
        // ========== Step 0: Build local graph ==========
        // Build rule index for efficient lookup
        std::unordered_map<size_t, Rule*> hashToAppliedRule;
        std::unordered_map<size_t, int> hashToIndex;
        std::vector<size_t> indexToHash(appliedRules.size());
        
        for (size_t i = 0; i < appliedRules.size(); i++) {
            size_t ruleHash = appliedRules[i]->getRuleHash();
            hashToAppliedRule[ruleHash] = appliedRules[i];
            hashToIndex[ruleHash] = i;
            indexToHash[i] = ruleHash;
        }
        
        // Build combo2count and find fulfilled combos
        std::unordered_map<Combo*, int> combo2count;
        std::vector<Combo*> fulfilledCombos;
        
        for (Rule* rule : appliedRules) {
            size_t ruleHash = rule->getRuleHash();
            if (ruleHashToCombos.count(ruleHash)) {
                for (Combo* combo : ruleHashToCombos.at(ruleHash)) {
                    combo2count[combo]++;
                    if (combo2count[combo] == combo->length) {
                        fulfilledCombos.push_back(combo);
                    }
                }
            }
        }
        
        // Separate positive and negative edges
        // E+ = {(i,j) | lift_ij > 0 and i,j in R}
        // E- = {(i,j) | lift_ij < 0 and i,j in R}
        struct Edge {
            int i, j;           // indices in appliedRules
            size_t hash1, hash2; // rule hashes
            double lift;
            Combo* combo;
        };
        
        std::vector<Edge> positiveEdges;
        std::vector<Edge> negativeEdges;
        
        for (Combo* combo : fulfilledCombos) {
            double lift = combo->getSurprisalLift();
            
            if (hashToIndex.count(combo->ruleHash1) && hashToIndex.count(combo->ruleHash2)) {
                Edge edge;
                edge.hash1 = combo->ruleHash1;
                edge.hash2 = combo->ruleHash2;
                edge.i = hashToIndex[combo->ruleHash1];
                edge.j = hashToIndex[combo->ruleHash2];
                edge.lift = lift;
                edge.combo = combo;
                
                if (lift > 0) {
                    positiveEdges.push_back(edge);
                } else if (lift < 0) {
                    negativeEdges.push_back(edge);
                }
            }
        }
        
        if (shouldDebug && (!positiveEdges.empty() || !negativeEdges.empty())) {
            std::cout << "\n  Candidate " << candidate << " (" 
                      << data.getIndex()->getStringOfNodeId(candidate) << "):" << std::endl;
            std::cout << "    Applied Rules: " << appliedRules.size() << std::endl;
            std::cout << "    Fulfilled Combos: " << fulfilledCombos.size() << std::endl;
            std::cout << "    Positive Edges (E+): " << positiveEdges.size() << std::endl;
            std::cout << "    Negative Edges (E-): " << negativeEdges.size() << std::endl;
            std::cout << "    Original Surprisal: " << aggregatedSurprisal[candidate] << std::endl;
        }
        
        // ========== Apply Negative Method: Determine retained rules R' ==========
        std::unordered_set<int> retainedIndices;
        for (size_t i = 0; i < appliedRules.size(); i++) {
            retainedIndices.insert(i);
        }
        
        if (comboHandler.getNoisyorNegativeMethod() == "none") {
            // Keep all rules, do nothing
            
        } else if (comboHandler.getNoisyorNegativeMethod() == "prune") {
            // ========== Prune Method: Domination Pruning ==========
            // For each negative edge, delete the weaker rule
            
            std::unordered_set<int> deleteList;
            
            for (const Edge& edge : negativeEdges) {
                double w_i = appliedRules[edge.i]->getSurprisal();
                double w_j = appliedRules[edge.j]->getSurprisal();
                
                // The weaker rule in each negative edge should be deleted
                if (w_i >= w_j) {
                    deleteList.insert(edge.j);  // j is weaker
                } else {
                    deleteList.insert(edge.i);  // i is weaker
                }
            }
            
            // Get retained indices R' = R \ delete_list
            for (int idx : deleteList) {
                retainedIndices.erase(idx);
            }
            
            if (shouldDebug && !deleteList.empty()) {
                std::cout << "    [PRUNE] Deleted " << deleteList.size() << " dominated rules:" << std::endl;
                for (int idx : deleteList) {
                    Rule* rule = appliedRules[idx];
                    std::cout << "      - " << rule->computeRuleString(data.getIndex())
                              << " (surprisal=" << rule->getSurprisal() << ")" << std::endl;
                }
            }
            
        } else if (comboHandler.getNoisyorNegativeMethod() == "cluster") {
            // ========== Cluster Method: Connected Components ==========
            // Build undirected graph from negative edges and find connected components
            
            int numRules = appliedRules.size();
            std::vector<std::vector<int>> adjList(numRules);
            
            for (const Edge& edge : negativeEdges) {
                adjList[edge.i].push_back(edge.j);
                adjList[edge.j].push_back(edge.i);
            }
            
            // Find connected components using DFS
            std::vector<bool> visited(numRules, false);
            std::vector<std::vector<int>> components;
            
            for (int i = 0; i < numRules; i++) {
                if (!visited[i]) {
                    std::vector<int> component;
                    std::vector<int> stack = {i};
                    
                    while (!stack.empty()) {
                        int node = stack.back();
                        stack.pop_back();
                        
                        if (visited[node]) continue;
                        visited[node] = true;
                        component.push_back(node);
                        
                        for (int neighbor : adjList[node]) {
                            if (!visited[neighbor]) {
                                stack.push_back(neighbor);
                            }
                        }
                    }
                    
                    components.push_back(component);
                }
            }
            
            // Select representative from each component (max surprisal)
            retainedIndices.clear();
            for (const auto& component : components) {
                int bestIdx = component[0];
                double maxSurprisal = appliedRules[bestIdx]->getSurprisal();
                
                for (int idx : component) {
                    double surprisal = appliedRules[idx]->getSurprisal();
                    if (surprisal > maxSurprisal) {
                        maxSurprisal = surprisal;
                        bestIdx = idx;
                    }
                }
                
                retainedIndices.insert(bestIdx);
            }
            
            if (shouldDebug) {
                int clusteredRules = appliedRules.size() - retainedIndices.size();
                if (clusteredRules > 0) {
                    std::cout << "    [CLUSTER] " << components.size() << " components, "
                              << "retained " << retainedIndices.size() << "/" << appliedRules.size() 
                              << " rules" << std::endl;
                    
                    // Show components with >1 rule
                    for (size_t compIdx = 0; compIdx < components.size(); compIdx++) {
                        const auto& comp = components[compIdx];
                        if (comp.size() > 1) {
                            std::cout << "      Component #" << (compIdx+1) << " (" << comp.size() << " rules):" << std::endl;
                            for (int idx : comp) {
                                Rule* rule = appliedRules[idx];
                                bool isRep = (retainedIndices.count(idx) > 0);
                                std::cout << "        " << (isRep ? "[REP] " : "      ")
                                          << rule->computeRuleString(data.getIndex())
                                          << " (surprisal=" << rule->getSurprisal() << ")" << std::endl;
                            }
                        }
                    }
                }
            }
        }
        
        // ========== Recalculate base surprisal for retained rules ==========
        double baseSurprisal = 0.0;
        for (int idx : retainedIndices) {
            baseSurprisal += appliedRules[idx]->getSurprisal();
        }
        
        // ========== Apply Positive Method: Synergy from positive edges ==========
        double addedSynergy = 0.0;
        
        if (comboHandler.getNoisyorPositiveMethod() == "none") {
            // No positive synergy
            
        } else if (comboHandler.getNoisyorPositiveMethod() == "mst") {
            // ========== MST Method: Maximum Spanning Forest on positive edges ==========
            // Only consider edges where BOTH endpoints are in R'
            
            std::vector<Edge> eligiblePositiveEdges;
            for (const Edge& edge : positiveEdges) {
                if (retainedIndices.count(edge.i) && retainedIndices.count(edge.j)) {
                    eligiblePositiveEdges.push_back(edge);
                }
            }
            
            if (!eligiblePositiveEdges.empty()) {
                // Sort edges by lift (descending) for Kruskal's MST
                std::sort(eligiblePositiveEdges.begin(), eligiblePositiveEdges.end(),
                    [](const Edge& a, const Edge& b) {
                        return a.lift > b.lift;
                    });
                
                // Union-Find for Kruskal's algorithm
                std::unordered_map<int, int> parent;
                std::unordered_map<int, int> rank_uf;
                
                // Initialize each retained node as its own parent
                for (int idx : retainedIndices) {
                    parent[idx] = idx;
                    rank_uf[idx] = 0;
                }
                
                // Find with path compression
                std::function<int(int)> find = [&](int x) -> int {
                    if (parent[x] != x) {
                        parent[x] = find(parent[x]);
                    }
                    return parent[x];
                };
                
                // Union by rank
                auto unionSets = [&](int x, int y) -> bool {
                    int px = find(x);
                    int py = find(y);
                    if (px == py) return false; // Already in same set (would create cycle)
                    
                    if (rank_uf[px] < rank_uf[py]) {
                        parent[px] = py;
                    } else if (rank_uf[px] > rank_uf[py]) {
                        parent[py] = px;
                    } else {
                        parent[py] = px;
                        rank_uf[px]++;
                    }
                    return true;
                };
                
                // Build MST (actually maximum spanning forest)
                std::vector<Edge> mstEdges;
                for (const Edge& edge : eligiblePositiveEdges) {
                    if (unionSets(edge.i, edge.j)) {
                        mstEdges.push_back(edge);
                        addedSynergy += comboHandler.getLiftRatio() * edge.lift;
                    }
                }
                
                if (shouldDebug && !mstEdges.empty()) {
                    std::cout << "    [MST] Selected " << mstEdges.size() << " tree edges:" << std::endl;
                    for (size_t i = 0; i < mstEdges.size(); i++) {
                        const Edge& edge = mstEdges[i];
                        std::cout << "      Edge #" << (i+1) << ": lift=" << edge.lift
                                  << " (scaled: " << (comboHandler.getLiftRatio() * edge.lift) << ")" << std::endl;
                    }
                    std::cout << "      Total synergy (alpha * sum(lifts)): " << addedSynergy << std::endl;
                }
            }
            
        } else if (comboHandler.getNoisyorPositiveMethod() == "matching1" || comboHandler.getNoisyorPositiveMethod() == "matching2") {
            // ========== b-Matching Method: Maximum Weight b-Matching on positive edges ==========
            // Greedy approximation: sort edges by weight, select if both endpoints' degree < b
            // matching1: b=1 (maximum matching), matching2: b=2
            
            int b = (comboHandler.getNoisyorPositiveMethod() == "matching1") ? 1 : 2;
            
            // Only consider edges where BOTH endpoints are in R'
            std::vector<Edge> eligiblePositiveEdges;
            for (const Edge& edge : positiveEdges) {
                if (retainedIndices.count(edge.i) && retainedIndices.count(edge.j)) {
                    eligiblePositiveEdges.push_back(edge);
                }
            }
            
            if (!eligiblePositiveEdges.empty()) {
                // Sort edges by lift (descending) for greedy selection
                std::sort(eligiblePositiveEdges.begin(), eligiblePositiveEdges.end(),
                    [](const Edge& a, const Edge& b) {
                        return a.lift > b.lift;
                    });
                
                // Track degree of each node
                std::unordered_map<int, int> nodeDegree;
                for (int idx : retainedIndices) {
                    nodeDegree[idx] = 0;
                }
                
                // Greedily select edges
                std::vector<Edge> selectedEdges;
                for (const Edge& edge : eligiblePositiveEdges) {
                    // Check if both endpoints have degree < b
                    if (nodeDegree[edge.i] < b && nodeDegree[edge.j] < b) {
                        selectedEdges.push_back(edge);
                        nodeDegree[edge.i]++;
                        nodeDegree[edge.j]++;
                        addedSynergy += comboHandler.getLiftRatio() * edge.lift;
                    }
                }
                
                if (shouldDebug && !selectedEdges.empty()) {
                    std::cout << "    [MATCHING" << b << "] Selected " << selectedEdges.size() << " edges:" << std::endl;
                    for (size_t i = 0; i < selectedEdges.size(); i++) {
                        const Edge& edge = selectedEdges[i];
                        std::cout << "      Edge #" << (i+1) << ": lift=" << edge.lift
                                  << " (scaled: " << (comboHandler.getLiftRatio() * edge.lift) << ")"
                                  << " [rule " << edge.i << " <-> rule " << edge.j << "]" << std::endl;
                    }
                    std::cout << "      Total synergy (alpha * sum(lifts)): " << addedSynergy << std::endl;
                    
                    // Show node degree statistics
                    std::cout << "      Node degree distribution:" << std::endl;
                    std::unordered_map<int, int> degreeCount;
                    for (const auto& pair : nodeDegree) {
                        if (pair.second > 0) {
                            degreeCount[pair.second]++;
                        }
                    }
                    for (int deg = 0; deg <= b; deg++) {
                        if (degreeCount.count(deg)) {
                            std::cout << "        Degree " << deg << ": " << degreeCount[deg] << " nodes" << std::endl;
                        }
                    }
                }
            }
        }
        
        // ========== Update aggregated surprisal ==========
        double newSurprisal = baseSurprisal + addedSynergy;
        aggregatedSurprisal[candidate] = newSurprisal;
        
        if (shouldDebug && (retainedIndices.size() < appliedRules.size() || addedSynergy > 0)) {
            std::cout << "    Final: base=" << baseSurprisal 
                      << ", synergy=" << addedSynergy 
                      << ", total=" << newSurprisal << std::endl;
        }
    }
    
    // Track ground truth ranking changes
    if (shouldDebug && groundTruthTargets != nullptr) {
        std::vector<std::pair<int, double>> afterRanking(aggregatedSurprisal.begin(), aggregatedSurprisal.end());
        std::sort(afterRanking.begin(), afterRanking.end(),
            [](const std::pair<int, double>& a, const std::pair<int, double>& b) {
                return a.second > b.second;
            });
        
        for (size_t i = 0; i < afterRanking.size(); i++) {
            int candId = afterRanking[i].first;
            if (gtRanksBefore.count(candId)) {
                gtRanksAfter[candId] = i + 1;
            }
        }
        
        std::cout << "\n  [GROUND TRUTH RANKING ANALYSIS]" << std::endl;
        for (const auto& pair : gtRanksBefore) {
            int gtId = pair.first;
            int rankBefore = pair.second;
            int rankAfter = gtRanksAfter[gtId];
            int rankChange = rankBefore - rankAfter;
            
            std::cout << "    Ground Truth " << gtId << " (\""
                      << data.getIndex()->getStringOfNodeId(gtId) << "\"):" << std::endl;
            std::cout << "      Rank before combo: " << rankBefore << std::endl;
            std::cout << "      Rank after combo: " << rankAfter << std::endl;
            if (rankChange > 0) {
                std::cout << "      Result: IMPROVED (moved up by " << rankChange << " positions)" << std::endl;
            } else if (rankChange < 0) {
                std::cout << "      Result: DEGRADED (moved down by " << (-rankChange) << " positions)" << std::endl;
            } else {
                std::cout << "      Result: NO CHANGE" << std::endl;
            }
        }
        
        std::cout << "==========================================\n" << std::endl;
    }
}

void ApplicationHandler::applyComboAdjustmentsMaxplus(
    std::unordered_map<int, std::vector<double>>& candToScoreList,
    std::unordered_map<int, std::vector<Rule*>>& candToRules,
    RuleStorage& rules,
    bool shouldDebug,
    TripleStorage& train,
    Rule*& bestRule,
    Combo*& bestCombo
) {
    if (!rules.hasCombos()) {
        return;
    }
    
    auto& ruleHashToCombos = rules.getRuleHashToCombos();
    
    for (auto& pair : candToRules) {
        int candidate = pair.first;
        std::vector<Rule*>& appliedRules = pair.second;
        
        std::vector<double>& scoreList = candToScoreList[candidate];
        
        // Sort applied rules by confidence (descending)
        std::sort(appliedRules.begin(), appliedRules.end(), 
            [](Rule* a, Rule* b) { return a->getConfidence() > b->getConfidence(); });
        bestRule = appliedRules.empty() ? nullptr : appliedRules[0];
        
        double maxConfBefore = scoreList.empty() ? 0.0 : scoreList[0];
        
        if (shouldDebug) {
            std::cout << "\nCandidate " << candidate << " has " << appliedRules.size() << " applied rules" << std::endl;
            std::cout << "  Max confidence before combo: " << maxConfBefore << std::endl;
        }
        
        // Find and add combo confidences if applicable
        bool foundCombo = false;
        bestCombo = nullptr;
        std::vector<Rule*> comboMemberRules;
        
        auto findCombo = [&]() {
            // Build combo2count
            std::unordered_map<Combo*, int> combo2count;
            std::unordered_map<Combo*, std::vector<Rule*>> comboToRules;
            int NotFoundStep = 0;

            for (Rule* rule : appliedRules) {
                size_t ruleHash = rule->getRuleHash();
                NotFoundStep++;
                if (ruleHashToCombos.count(ruleHash)) {
                    for (Combo* combo : ruleHashToCombos.at(ruleHash)) {
                        combo2count[combo]++;
                        comboToRules[combo].push_back(rule);
                        
                        if (combo2count[combo] == combo->length) {
                            // All rules in combo have been applied; add combo confidence
                            scoreList.push_back(combo->getConfidence());
                            
                            if (combo->getConfidence() > (bestCombo ? bestCombo->getConfidence() : 0.0)) {
                                NotFoundStep = 0;
                                bestCombo = combo;
                                comboMemberRules = comboToRules[combo];
                                foundCombo = true;
                            }
                        }
                    }
                    if (NotFoundStep >= 3 && bestCombo != nullptr) return;
                }
            }
        };
        
        if (!appliedRules.empty()) {
            findCombo();
        }
        
        // Sort scoreList in descending order for comparison
        std::sort(scoreList.begin(), scoreList.end(), std::greater<double>());
        
        double maxConfAfter = scoreList.empty() ? 0.0 : scoreList[0];
        bool maxConfChanged = (maxConfAfter != maxConfBefore);
        
        if (shouldDebug && foundCombo) {
            std::cout << "\n  [COMBO FOUND]" << " length: " << bestCombo->length 
                      << ", confidence: " << bestCombo->getConfidence() << std::endl;
            for (int i = 0; i < comboMemberRules.size(); i++) {
                Rule* memberRule = comboMemberRules[i];
                std::cout << "      Rule #" << (i+1) << ":" << memberRule->computeRuleString(train.getIndex()) << std::endl;
                std::cout << "        Confidence: " << memberRule->getConfidence() 
                          << ", ID: " << memberRule->getID() << std::endl;
            }
            
            std::cout << "\n    Max confidence changed: " << (maxConfChanged ? "YES" : "NO") << std::endl;
            if (maxConfChanged) {
                std::cout << "      Before: " << maxConfBefore << " -> After: " << maxConfAfter << std::endl;
                std::cout << "      Improvement: " << (maxConfAfter - maxConfBefore) << std::endl;
            }

            double bestRuleConf = (bestRule != nullptr) ? bestRule->getConfidence() : 0.0;
            double bestComboConf = (bestCombo != nullptr) ? bestCombo->getConfidence() : 0.0;
            if (bestCombo != nullptr && bestComboConf >= bestRuleConf) {
                std::cout << "      Max confidence source: COMBO (length=" << bestCombo->length 
                          << ", conf=" << bestCombo->getConfidence() << ")" << std::endl;
            } else if (bestRule != nullptr) {
                std::cout << "      Max confidence source: RULE (ID=" << bestRule->getID() 
                          << ", conf=" << bestRule->getConfidence() << ")" << std::endl;
            }
        } else if (shouldDebug && !foundCombo) {
            std::cout << "  [NO COMBO FOUND]" << std::endl;
        }
    }
}

// ================ End of Helper Functions ================





void ApplicationHandler::calculateTripleScores(std::vector<Triple> triples, TripleStorage& train, RuleStorage& rules){

    tripleScores.resize(triples.size());
    if (score_collectGr){
        tripleGroundings.resize(triples.size());
    }


    typedef void (ApplicationHandler::*SortAndProcessPtr)(std::vector<std::pair<int,double>>&, QueryResults&, TripleStorage&, RuleStorage&, int, int, bool, const int*, int);
    SortAndProcessPtr sortAndProcess = nullptr;

    std::string aggrFunc = comboHandler.getAggregationFunction();
    if(aggrFunc=="noisyor") {
        sortAndProcess = &ApplicationHandler::sortAndProcessNoisy;
    } else if (aggrFunc=="maxplus") {
        sortAndProcess = &ApplicationHandler::sortAndProcessMax;
    }else{
        throw std::runtime_error("Dont understand the aggregation function.");
    }

    #pragma omp parallel num_threads(num_thr)
    {
        QueryResults tripleResults(1, 1);
        // we dont need to set num_top_rules as the stopping is handled outside; there is only one "candidate"
        tripleResults.setAggrFunc(aggrFunc);
        RuleGroundings ruleGroundings;   
        #pragma omp for schedule(dynamic)
        for (int i=0; i<triples.size(); i++){
           
            if (verbose && i%1000==0 && i>0){
                std::cout<<"Scored "<<(i/1000) * 1000<<" triples..."<<std::endl;
            }
            Triple triple = triples[i];
            int head = triple[0];
            int rel = triple[1];
            int tail = triple[2];
            auto& relRules = rules.getRelRules(rel);
            
            //triple = head, rel, tail
            int ctr = 0;
            for (Rule* rule: relRules){
                bool madePred;
                if (score_collectGr){
                    madePred = rule->predictTriple(head, tail, train, tripleResults, &ruleGroundings);
                }else{
                    madePred = rule->predictTriple(head, tail, train, tripleResults, nullptr);
                }
                        
                if (madePred){
                    ctr+= 1;
                }
                if (ctr>=score_numTopRules && score_numTopRules>0){
                    break;
                }
            }

            // we actually only have one candidate but we still need to process
            std::unordered_map<int, double>& candScores = tripleResults.getCandScores();
            std::vector<std::pair<int, double>> sortedCandScores(candScores.begin(), candScores.end());
            
            // tie handling, final processing, sorting (no query context for triple scoring)
            (this->*sortAndProcess)(sortedCandScores, tripleResults, train, rules, -1, -1, true, nullptr, 0);

            #pragma omp critical
            {  
                double trScore = 0;
                if (sortedCandScores.size()>0){
                    trScore = sortedCandScores[0].second;
                }
                // for easy conversion later
                tripleScores.at(i) = { (double) triple[0],  (double) triple[1], (double) triple[2], trScore};  
                if (score_collectGr){
                    tripleGroundings.at(i) = std::make_pair(triple, ruleGroundings);
                }    
            }
            tripleResults.clear();
            ruleGroundings.clear();
        }
    }
}

void ApplicationHandler::calculateQueryResults(TripleStorage& target, TripleStorage& train, RuleStorage& rules, TripleStorage& addFilter, bool dirIsTail){
    // define rule prediction function depending on direction
    typedef bool (Rule::*RulePredFunc)(int, TripleStorage&, QueryResults&, ManySet);
    RulePredFunc predictHeadOrTail;

    if(dirIsTail){
        predictHeadOrTail = &Rule::predictTailQuery;
    }else{
        predictHeadOrTail = &Rule::predictHeadQuery;
    }

    typedef void (ApplicationHandler::*SortAndProcessPtr)(std::vector<std::pair<int,double>>&, QueryResults&, TripleStorage&, RuleStorage&, int, int, bool, const int*, int);
    SortAndProcessPtr sortAndProcess = nullptr;

    std::string aggrFunc = comboHandler.getAggregationFunction();
    if(aggrFunc=="noisyor") {
        sortAndProcess = &ApplicationHandler::sortAndProcessNoisy;
    } else if (aggrFunc=="maxplus") {
        sortAndProcess = &ApplicationHandler::sortAndProcessMax;
    }else{
        throw std::runtime_error("Dont understand the aggregation function.");
    }



    if (verbose && dirIsTail){
        std::cout<<"Calculating tail queries.."<<std::endl;
    }else if (verbose){
        std::cout<<"Calculating head queries.."<<std::endl;
        
    }

    
    int numNodes = train.getIndex()->getNodeSize();
    int numRel = train.getIndex()->getRelSize();
    // size is num triples not queries 
    int chunk = std::min(10000, std::max(1000, (target.getSize())/50));


    std::vector<std::tuple<int,int,int>> tasks;

    for (int rel=0; rel<numRel; rel++){
        for (int source=0; source<numNodes; source++){
                int* begin;
                int length;
                dirIsTail ? target.getTforHR(source, rel, begin, length) : target.getHforTR(source, rel, begin, length);
                if (length>0){
                    tasks.emplace_back(rel, source, length);
                }
        }
    }
    int ctr=0;
    #pragma omp parallel num_threads(num_thr)
    {
        QueryResults qResults(rank_topk, rank_discAtLeast);
        qResults.setPerformAggregation(performAggregation);
        qResults.setAggrFunc(aggrFunc);
        qResults.setNumTopRules(score_numTopRules);
        ManySet filter;
        #pragma omp for schedule(dynamic)
        for (int i=0; i<tasks.size(); i++){
            int rel = std::get<0>(tasks[i]);
            int source = std::get<1>(tasks[i]);
            int length = std::get<2>(tasks[i]);

            int adapted_topk = rank_topk;   
            if (adapt_topk){
                adapted_topk = rank_topk + length;
                qResults.setAddTopK(adapted_topk);
            }
            ctr+=1;
            if (verbose && ctr%chunk==0 && dirIsTail){
                std::cout<<"Calculated "<< (ctr/chunk) * chunk <<" tail queries..."<<std::endl;
            }else if (verbose && ctr%chunk==0){
                std::cout<<"Calculated "<< (ctr/chunk) * chunk <<" head queries..."<<std::endl;
            }
            auto& relRules = rules.getRelRules(rel);
             // filtering for train and additionalFilter
            if (rank_filterWtrain){
                Nodes* trainFilter = nullptr;
                trainFilter = (!dirIsTail) ? train.getHforTR(source, rel) : train.getTforHR(source, rel);
                if (trainFilter){
                    filter.addSet(trainFilter);
                }   
                        
            }
            // always filter with additionalFilter (can be empty)
            Nodes* naddFilter = nullptr;
            naddFilter = (!dirIsTail) ? addFilter.getHforTR(source, rel) : addFilter.getTforHR(source, rel);
            if (naddFilter){
                filter.addSet(naddFilter);
            }
            // perform rule application
            int ctr = 0;
            int currSize = 0;
            for (Rule* rule : relRules){
                ctr += 1;
                (rule->*predictHeadOrTail)(source, train, qResults, filter);
                currSize = qResults.size();
                if (rank_numPreselect>0 && currSize>=rank_numPreselect){
                    break;
                }
                // possibly can be optimized
                // checking for discrimination after every rule had no noticeable overhead
                if (currSize>=adapted_topk){
                    if (rank_discAtLeast>0){
                         if (qResults.checkDiscrimination()){
                            break;
                         }
                    }
                    if (score_numTopRules>0){
                        if (qResults.checkNumTopRules()){
                             break;
                        }
                    }
                 }
            }

            std::vector<std::pair<int, double>> sortedCandScores;
            // tie handling, final processing, sorting
            if (performAggregation){
                // Get ground truth targets for this query
                int* gtBegin;
                int gtLength;
                dirIsTail ? target.getTforHR(source, rel, gtBegin, gtLength) : target.getHforTR(source, rel, gtBegin, gtLength);
                (this->*sortAndProcess)(sortedCandScores, qResults, train, rules, rel, source, dirIsTail, gtBegin, gtLength);
            }
                    

            #pragma omp critical
            {   
                if (saveCandidateRules){
                    // TODO when needed could prevent copy here by using shared pointer
                    if (dirIsTail){
                        tailQcandsRules[rel][source] = qResults.getCandRules();
                    }else{
                        headQcandsRules[rel][source] = qResults.getCandRules();
                    }
                }
                if (performAggregation){
                        auto& writeResults = (dirIsTail) ? tailQcandsConfs : headQcandsConfs;
                        writeResults[rel][source] = sortedCandScores;
                }
            }
            qResults.clear();
            filter.clear();
        } 
    } //pragma
}

void ApplicationHandler::sortAndProcessNoisy(std::vector<std::pair<int,double>>& candScoresToSort, QueryResults& qResults, TripleStorage& data, RuleStorage& rules, int queryRel, int querySource, bool queryDirIsTail, const int* groundTruthTargets, int numGroundTruth){
    // noisyor scoring is already performed in QueryResults
    // candScores contains aggregated surprisal values
    // Under independent rule assumption, higher aggregated surprisal means lower failure probability

    // Debug tracking - only for first few queries on thread 0
    // No mutex needed: only thread 0 enters this block, and it processes one query at a time
    static std::atomic<int> queryCount(0);
    bool shouldDebug = false;
    int threadNum = omp_get_thread_num();
    
    // Only thread 0 checks debug; atomic operation ensures thread safety
    if (threadNum == 0 && rules.hasCombos()) {
        int currentCount = queryCount.fetch_add(1, std::memory_order_relaxed);
        if (currentCount < comboHandler.getQueryTopK()) {
            shouldDebug = true;
            
            // For link prediction, output full query info
            if (queryRel >= 0) {
                debugOutputQueryInfo(queryCount, querySource, queryRel, queryDirIsTail, 
                                   groundTruthTargets, numGroundTruth, qResults.getCandRules(), rules, data);
            }
            // For triple classification, output simplified triple info
            else {
                Index* index = data.getIndex();
                std::cout << "\n========== Triple #" << queryCount << " Scoring Debug (Thread 0) ==========" << std::endl;
                // Note: querySource contains head, queryDirIsTail is always true for triples
                // We need to get the actual triple info from qResults
                auto& candRules = qResults.getCandRules();
                if (!candRules.empty()) {
                    int tail = candRules.begin()->first;
                    std::cout << "  Aggregation Method: " << comboHandler.getAggregationFunction() << std::endl;
                    std::cout << "  Combo Positive Method: " << comboHandler.getNoisyorPositiveMethod() << std::endl;
                    std::cout << "  Combo Negative Method: " << comboHandler.getNoisyorNegativeMethod() << std::endl;
                    std::cout << "  Combo Lift Ratio: " << comboHandler.getLiftRatio() << std::endl;
                    
                    std::vector<Rule*>& appliedRules = candRules.begin()->second;
                    std::cout << "\n  Applied Rules: " << appliedRules.size() << std::endl;
                    
                    // Show first 10 rules
                    int maxRulesToShow = std::min(10, (int)appliedRules.size());
                    for (int ri = 0; ri < maxRulesToShow; ri++) {
                        Rule* rule = appliedRules[ri];
                        std::cout << "    Rule #" << (ri+1) << ": " << rule->computeRuleString(index)
                                  << ", conf=" << rule->getConfidence()
                                  << ", surprisal=" << rule->getSurprisal() << std::endl;
                    }
                    if (appliedRules.size() > maxRulesToShow) {
                        std::cout << "    ... (" << (appliedRules.size() - maxRulesToShow) << " more rules)" << std::endl;
                    }
                    
                    std::cout << "\n  Aggregated Surprisal (before combo): " 
                              << qResults.getCandScores().at(tail) << std::endl;
                }
            }
        }
    }

    // Get aggregated surprisal from query results
    std::unordered_map<int, double> aggregatedSurprisal = qResults.getCandScores();
    auto& candRules = qResults.getCandRules();
    
    // Apply combo adjustments (negative: prune/cluster, positive: mst)
    applyComboAdjustmentsNoisyor(aggregatedSurprisal, candRules, rules, shouldDebug, data, 
                                  groundTruthTargets, numGroundTruth);

    candScoresToSort.assign(aggregatedSurprisal.begin(), aggregatedSurprisal.end()); 

    // Debug output for triple classification: show surprisal after combo
    if (shouldDebug && queryRel == -1) {
        std::cout << "\n  Aggregated Surprisal (after combo): ";
        if (!candScoresToSort.empty()) {
            std::cout << candScoresToSort[0].second << std::endl;
        } else {
            std::cout << "0" << std::endl;
        }
    }

    // Sort by aggregated surprisal (higher is better - lower failure probability)
    if (rank_tie_handling=="random"){
        std::sort(
            candScoresToSort.begin(),
            candScoresToSort.end(), 
            [](const std::pair<int, double>& a, const std::pair<int, double>& b) {
                return a.second > b.second;
            }
        );
    }else if (rank_tie_handling=="frequency"){
        std::sort(
            candScoresToSort.begin(),
            candScoresToSort.end(), 
            [&data](const std::pair<int, double>& a, const std::pair<int, double>& b) {
                if (a.second!=b.second){
                    return a.second > b.second;
                }else if(data.getFreq(a.first) != data.getFreq(b.first)) {
                    return data.getFreq(a.first) > data.getFreq(b.first);
                }else{
                    return a.first<b.first;
                }
            }
        );
    }else{
        throw std::runtime_error("Tie handling type not known. Please set to 'random' or 'frequency'");
    }

    // Convert from aggregated surprisal to final probability: 1 - exp(-aggregatedSurprisal)
    for (auto& pair: candScoresToSort){
        double aggregatedSurprisal = pair.second;
        pair.second = 1.0 - std::exp(-aggregatedSurprisal);
    }
    
    // Debug output for triple classification: show final confidence
    if (shouldDebug && queryRel == -1) {
        std::cout << "  Final Confidence Score: ";
        if (!candScoresToSort.empty()) {
            std::cout << candScoresToSort[0].second << " (1 - exp(-surprisal))" << std::endl;
        } else {
            std::cout << "0" << std::endl;
        }
        std::cout << "==========================================\n" << std::endl;
    }
}

void ApplicationHandler::sortAndProcessMax(std::vector<std::pair<int,double>>& candScoresToSort, QueryResults& qResults, TripleStorage& data, RuleStorage& rules, int queryRel, int querySource, bool queryDirIsTail, const int* groundTruthTargets, int numGroundTruth){
    scoreMaxPlus(qResults.getCandRules(), candScoresToSort, data, rules, queryRel, querySource, queryDirIsTail, groundTruthTargets, numGroundTruth);
}

// currently not used in the ranking process
void ApplicationHandler::aggregateQueryResults(std::string direction, TripleStorage& train, RuleStorage& rules){
    auto& queryResults = (direction=="tail") ? tailQcandsRules : headQcandsRules;
    auto& writeResults = (direction=="tail") ? tailQcandsConfs : headQcandsConfs;
    for (auto& queries: queryResults){
            int relation = queries.first;
            std::unordered_map<int, NodeToPredRules>& srcToCand = queries.second;
            for (auto& query: srcToCand){
                int source = query.first; 
                std::string aggrFunc = comboHandler.getAggregationFunction();
                if (aggrFunc=="maxplus"){
                    scoreMaxPlus(query.second, writeResults[relation][source], train, rules);
                }else{
                    throw std::runtime_error("Aggregation function is not recognized in calculate ranking.");
                }
                
            }
    }
}

// note this does not yet filter with target as ranking is performed query based; filtering with target only happens when writing the ranking
void ApplicationHandler::makeRanking(TripleStorage& target, TripleStorage& train, RuleStorage& rules, TripleStorage& addFilter){
    if (rank_tie_handling=="frequency"){
        if (verbose){
            std::cout<<"Calculate entity frequencies..."<<std::endl;
        }
        
        train.calcEntityFreq();
    }
    calculateQueryResults(target, train, rules, addFilter, true);
    calculateQueryResults(target, train, rules, addFilter, false);
}

// query results must have been calculated before and aggregated
void ApplicationHandler::writeRanking(TripleStorage& target, std::string filepath){
    Index* index = target.getIndex();
    RelNodeToNodes& data = target.getRelTailToHeads();
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw  std::runtime_error("Failed to create file. Please check if the paths are correct: " + filepath );
    }
    for (auto& relQueries: data){
        int relation = relQueries.first;
        for (auto& srcTocands: relQueries.second){
            int tail = srcTocands.first;
            // true heads
            Nodes& trueHeads = srcTocands.second;
            // we use this direction to iterate over all triples
            // head relation tail is one triple of the target set
            for (int head: trueHeads){
                if (file.is_open()){
                    file<<index->getStringOfNodeId(head)<<" "<<index->getStringOfRelId(relation)<<" "<<index->getStringOfNodeId(tail)<<std::endl;
                    file<<"Heads: ";
                    // write head ranking
                    CandidateConfs& resultsHead = headQcandsConfs[relation][tail];
                    int numWritten = 0;
                    for (int i=0; i<resultsHead.size(); i++){
                        auto pair = resultsHead[i];
                        int predHead = pair.first;
                        double score = pair.second;
                        // filter with target
                        // current predicted head is excluded if its the true answer to some other query
                        if (rank_filterWtarget && !(predHead==head)){
                            if (!(trueHeads.find(predHead)==trueHeads.end())){
                                continue;
                            }
                        }
                        file<<index->getStringOfNodeId(predHead)<<"\t"<<score<<"\t";
                        numWritten += 1;
                        if (numWritten==rank_topk){
                            break;
                        }
                    }
                    // write tail ranking
                    file<<"\nTails: ";
                    //true tails for filtering
                    Nodes& trueTails = target.getRelHeadToTails()[relation][head];
                    CandidateConfs& resultsTail = tailQcandsConfs[relation][head];
                    numWritten = 0;
                    for (int i=0; i<resultsTail.size(); i++){
                        auto pair = resultsTail[i];
                        int predTail = pair.first;
                        double score = pair.second;
                        if (rank_filterWtarget && !(predTail==tail)){
                            if (!(trueTails.find(predTail)==trueTails.end())){
                                continue;
                            }
                        }
                        file<<index->getStringOfNodeId(predTail)<<"\t"<<score<<"\t";
                        numWritten += 1;
                        if (numWritten==rank_topk){
                            break;
                        }
                    }
                    file<<"\n";
                }
            }
        }
    }
    file.close();
    std::cout<<"Ranking file written to:  " + filepath <<std::endl; 
}

// query results must have been calculated before and aggregated
void ApplicationHandler::writeRules(TripleStorage& target, std::string filepath, std::string direction, bool strings){
    
    if ((this->headQcandsRules.size() == 0 && this->tailQcandsRules.size() == 0) || !saveCandidateRules){
        throw std::runtime_error(
            "Please calculate answers using calculate_ranking() and set in the options ranking_handler.collect_rules to true first."
        );
    }

    Index* index = target.getIndex();
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw  std::runtime_error("Failed to create file. Please check if the paths are correct: " + filepath );
    }

    auto& data = (direction == "head") ? this->headQcandsRules : this->tailQcandsRules;

    for (auto& relQueries: data){
        int relation = relQueries.first;
        std::string relationStr = strings ? "\"" + index->getStringOfRelId(relation) + "\"" : std::to_string(relation);
        for (auto& srcQueries: relQueries.second){
            int src = srcQueries.first;
            std::string srcStr = strings ? "\"" + index->getStringOfNodeId(src) + "\"" : std::to_string(src);
        
            // Collect answers and rules
            std::string answers = "";
            std::string rules = "";

            auto itr = srcQueries.second.begin();
            for(; itr != srcQueries.second.end(); itr++){
                int to = itr->first;
                std::string toStr = strings ? "\"" + index->getStringOfNodeId(to) + "\"" : std::to_string(to);
                answers += toStr;
                if (std::next(itr) != srcQueries.second.end()) {
                    answers += ",";
                }

                std::string ruleset = "[";
                for(int ridx = 0; ridx < itr->second.size(); ridx++){
                    ruleset += strings ? "\"" + itr->second[ridx]->computeRuleString(index) + "\"" : std::to_string(itr->second[ridx]->getID());
                    if (ridx < itr->second.size() - 1){
                        ruleset += ",";
                    }
                }
                ruleset += "]";
                rules += ruleset;
                if (std::next(itr) != srcQueries.second.end()) {
                    rules += ",";
                }
            }
            file << "{\"query\": [" << srcStr << "," << relationStr << "], \"answers\": [" << answers << "], \"rules\": [" << rules << "]}" << std::endl;
        }
    }
    file.close();
    std::cout<<"Rules file written to:  " + filepath <<std::endl; 
}

void ApplicationHandler::scoreMaxPlus(
    const NodeToPredRules& candToRules, std::vector<std::pair<int, double>>& aggrCand, TripleStorage& train, RuleStorage& rules,
    int queryRel, int querySource, bool queryDirIsTail, const int* groundTruthTargets, int numGroundTruth
     ){
    
    // Pre-compute score lists for all candidates
    std::unordered_map<int, std::vector<double>> candToScoreList;
    std::unordered_map<int, std::vector<double>> candToScoreListBeforeCombo; // For comparison
    
    // Debug: Track first few queries for detailed analysis - ONLY thread 0
    // No mutex needed: only thread 0 enters this block, and it processes one query at a time
    static std::atomic<int> queryCount(0);
    bool shouldDebug = false;
    int threadNum = omp_get_thread_num();
    Rule* bestRule = nullptr;
    Combo* bestCombo = nullptr;
    
    // Only thread 0 checks debug; atomic operation ensures thread safety
    if (threadNum == 0 && queryRel >= 0) {
        int currentCount = queryCount.fetch_add(1, std::memory_order_relaxed);
        if (currentCount < comboHandler.getQueryTopK()) {
            shouldDebug = true;
            
            // Use helper function for debug output
            std::unordered_map<int, std::vector<Rule*>> candRulesCopy(candToRules.begin(), candToRules.end());
            debugOutputQueryInfo(queryCount, querySource, queryRel, queryDirIsTail, 
                               groundTruthTargets, numGroundTruth, candRulesCopy, rules, train);
            
            // Additional maxplus-specific info
            std::unordered_set<Rule*> appliedRulesSet;
            for (const auto& pair : candToRules) {
                for (Rule* rule : pair.second) {
                    appliedRulesSet.insert(rule);
                }
            }
            
            auto& relRules = rules.getRelRules(queryRel);
            std::cout << "  Applied Rules: " << appliedRulesSet.size() 
                      << " out of " << relRules.size() << " total rules for relation " << queryRel << std::endl;
            
            // Show top applied rules by confidence
            std::vector<Rule*> sortedAppliedRules(appliedRulesSet.begin(), appliedRulesSet.end());
            std::sort(sortedAppliedRules.begin(), sortedAppliedRules.end(), 
                [](Rule* a, Rule* b) { return a->getConfidence() > b->getConfidence(); });
            
            int numToShow = std::min(10, (int)sortedAppliedRules.size());
            std::cout << "  Top " << numToShow << " Applied Rules by Confidence:" << std::endl;
            for (int i = 0; i < numToShow; i++) {
                Rule* rule = sortedAppliedRules[i];
                auto stats = rule->getStats(false);
                std::cout << "    #" << (i+1) << ": " << rule->computeRuleString(train.getIndex()) << std::endl;
                std::cout << "        Confidence: " << rule->getConfidence() 
                          << ", NumTrue: " << stats[0]
                          << ", NumPred: " << stats[1]
                          << ", ID: " << rule->getID() << std::endl;
            }
        }
    }

    // Build scoreList for each candidate
    std::unordered_map<int, std::vector<Rule*>> candToRulesMutable;
    for (const auto& pair : candToRules) {
        int candidate = pair.first;
        std::vector<Rule*> appliedRules = pair.second;
        
        std::vector<double> scoreList;
        std::vector<double> scoreListBeforeCombo;

        // Sort applied rules by confidence (descending)
        std::sort(appliedRules.begin(), appliedRules.end(), 
            [](Rule* a, Rule* b) { return a->getConfidence() > b->getConfidence(); });
        
        // Build scoreList with single rule confidences
        for (Rule* rule : appliedRules) {
            scoreList.push_back(rule->getConfidence());
            scoreListBeforeCombo.push_back(rule->getConfidence());
        }
        
        candToScoreList[candidate] = scoreList;
        candToScoreListBeforeCombo[candidate] = scoreListBeforeCombo;
        candToRulesMutable[candidate] = appliedRules;
    }
    
    // Apply combo adjustments using helper function
    applyComboAdjustmentsMaxplus(candToScoreList, candToRulesMutable, rules, shouldDebug, train, bestRule, bestCombo);

    if (shouldDebug) {
        std::cout << "\n[RANKING PHASE]" << std::endl;
        std::cout << "  Sorting " << candToRules.size() << " candidates by lexicographic order..." << std::endl;
    }

    // Store original order for comparison and find ground truth positions
    std::vector<std::pair<int, std::vector<Rule*>>> candsToSort(candToRulesMutable.begin(), candToRulesMutable.end());
    std::vector<int> originalOrder;
    std::unordered_map<int, int> candToOrigRank;
    std::unordered_map<int, int> gtPositionsBefore;
    std::unordered_map<int, int> gtPositionsAfter;

    auto sortLexicographic = [&train, &candToScoreList, this](
        const std::pair<int, std::vector<Rule*>>& candA, 
        const std::pair<int, std::vector<Rule*>>& candB) {
        
        const std::vector<double>& scoresA = candToScoreList.at(candA.first);
        const std::vector<double>& scoresB = candToScoreList.at(candB.first);
        
        // Compare score lists lexicographically
        size_t minSize = std::min(scoresA.size(), scoresB.size());
        for (size_t i = 0; i < minSize; i++) {
            if (scoresA[i] > scoresB[i]) return true;
            if (scoresB[i] > scoresA[i]) return false;
        }
        
        // If all compared scores are equal, rank by number of scores
        if (scoresA.size() > scoresB.size()) return true;
        if (scoresB.size() > scoresA.size()) return false;
        
        // Tie handling for exactly same scores
        if (this->rank_tie_handling == "random") {
            return false;
        } else if (this->rank_tie_handling == "frequency") {
            if (train.getFreq(candA.first) != train.getFreq(candB.first)) {
                return train.getFreq(candA.first) > train.getFreq(candB.first);
            } else {
                return candA.first < candB.first;
            }
        } else {
            throw std::runtime_error("Could not understand tie_handling_parameter in scoreMaxPlus.");
        }
    };

    auto sortLexicographicBeforeCombo = [&train, &candToScoreListBeforeCombo, this](
        const std::pair<int, std::vector<Rule*>>& candA, 
        const std::pair<int, std::vector<Rule*>>& candB) {
        
        const std::vector<double>& scoresA = candToScoreListBeforeCombo.at(candA.first);
        const std::vector<double>& scoresB = candToScoreListBeforeCombo.at(candB.first);
        
        size_t minSize = std::min(scoresA.size(), scoresB.size());
        for (size_t i = 0; i < minSize; i++) {
            if (scoresA[i] > scoresB[i]) return true;
            if (scoresB[i] > scoresA[i]) return false;
        }
        
        if (scoresA.size() > scoresB.size()) return true;
        if (scoresB.size() > scoresA.size()) return false;
        
        if (this->rank_tie_handling == "random") {
            return false;
        } else if (this->rank_tie_handling == "frequency") {
            if (train.getFreq(candA.first) != train.getFreq(candB.first)) {
                return train.getFreq(candA.first) > train.getFreq(candB.first);
            } else {
                return candA.first < candB.first;
            }
        } else {
            throw std::runtime_error("Could not understand tie_handling_parameter in scoreMaxPlus.");
        }
    };
    
    // Sort by before-combo scores and fill originalOrder and gtPositionsBefore
    if (shouldDebug) {
        std::sort(candsToSort.begin(), candsToSort.end(), sortLexicographicBeforeCombo);
        for (int i = 0; i < candsToSort.size(); i++) {
            int candId = candsToSort[i].first;
            originalOrder.push_back(candId);
            candToOrigRank[candId] = i;
            
            // Check if this candidate is in ground truth
            if (groundTruthTargets != nullptr) {
                for (int j = 0; j < numGroundTruth; j++) {
                    if (candId == groundTruthTargets[j]) {
                        gtPositionsBefore[candId] = i + 1; // 1-indexed rank
                        break;
                    }
                }
            }
        }
    }

    // Final sort with combo scores
    std::sort(candsToSort.begin(), candsToSort.end(), sortLexicographic);
    
    if (shouldDebug) {
        // Find ground truth positions after sorting
        for (int i = 0; i < candsToSort.size(); i++) {
            int candId = candsToSort[i].first;
            if (gtPositionsBefore.count(candId) > 0) {
                gtPositionsAfter[candId] = i + 1;
            }
        }
        
        std::cout << "  Top 10 candidates after sorting:" << std::endl;
        for (int i = 0; i < std::min(10, (int)candsToSort.size()); i++) {
            int candId = candsToSort[i].first;
            const std::vector<double>& scores = candToScoreList.at(candId);
            double maxConf = scores.empty() ? 0.0 : scores[0];
            
            bool orderChanged = (i < originalOrder.size() && originalOrder[i] != candId);
            bool isGroundTruth = (gtPositionsBefore.count(candId) > 0);
            int origRank = candToOrigRank.at(candId) + 1;
            
            std::cout << "    Rank " << (i+1) << ": Candidate " << candId 
                      << ", MaxConf=" << maxConf 
                      << ", MaxConfBefore=" << candToScoreListBeforeCombo.at(candId).front() 
                      << ", origRank=" << origRank
                      << ", NumScores=" << scores.size();
            if (orderChanged) std::cout << " [ORDER CHANGED]";
            if (isGroundTruth) std::cout << " [GROUND TRUTH]";
            std::cout << std::endl;
        }
        
        // Analyze ground truth ranking changes
        if (!gtPositionsBefore.empty()) {
            std::cout << "\n  [GROUND TRUTH RANKING ANALYSIS]" << std::endl;
            for (const auto& pair : gtPositionsBefore) {
                int gtId = pair.first;
                int rankBefore = pair.second;
                int rankAfter = gtPositionsAfter[gtId];
                int rankChange = rankBefore - rankAfter;
                
                std::cout << "    Ground Truth " << gtId << ":" << std::endl;
                std::cout << "      Rank before combo: " << rankBefore << std::endl;
                std::cout << "      Rank after combo: " << rankAfter << std::endl;
                if (rankChange > 0) {
                    std::cout << "      Result: IMPROVED (moved up by " << rankChange << " positions)" << std::endl;
                } else if (rankChange < 0) {
                    std::cout << "      Result: DEGRADED (moved down by " << (-rankChange) << " positions)" << std::endl;
                } else {
                    std::cout << "      Result: NO CHANGE" << std::endl;
                }
            }
        }
        
        std::cout << "==========================================\n" << std::endl;
    }
    
    // Take sorted candidates and use their highest score
    for (const auto& pair : candsToSort) {
        const std::vector<double>& scores = candToScoreList.at(pair.first);
        double maxConf = scores.empty() ? 0.0 : scores[0];
        aggrCand.push_back(std::make_pair(pair.first, maxConf));
    }
}

void ApplicationHandler::clearAll(){
    headQcandsRules.clear();
    headQcandsConfs.clear();
    tailQcandsRules.clear();
    tailQcandsConfs.clear();
    tripleScores.clear();
    tripleGroundings.clear();
}
 
void ApplicationHandler::setNumPreselect(int num){
    rank_numPreselect=num;
}

void ApplicationHandler::setTopK(int topk){
    rank_topk=topk;
}
void ApplicationHandler::setFilterWTrain(bool ind){
    rank_filterWtrain=ind;
}

void ApplicationHandler::setFilterWtarget(bool ind){
    rank_filterWtarget = ind;
}
void ApplicationHandler::setAggregationFunc(std::string func){
    if (!(func=="maxplus") && !(func=="noisyor")){
        throw std::runtime_error("The aggregation function value is not known, select from 'noisyor' or 'maxplus' found value: " + func);
    }
    // Set to comboHandler instead of local variable
    comboHandler.setAggregationFunction(func);
}

void ApplicationHandler::setSaveCandidateRules(bool ind){
    saveCandidateRules = ind;
}
void  ApplicationHandler::setPerformAggregation(bool ind){
    performAggregation = ind;
}

void ApplicationHandler::setDiscAtLeast(int num){
    rank_discAtLeast = num;
}

void ApplicationHandler::setTieHandling(std::string opt){
    rank_tie_handling = opt;
}

void ApplicationHandler::setVerbose(bool ind){
    verbose = ind;
}

void ApplicationHandler::setScoreCollectGroundings(bool ind){
    score_collectGr = ind;
}

bool ApplicationHandler::getScoreCollectGroundings(){
    return score_collectGr;
}

void ApplicationHandler::setScoreNumTopRules(int num){
    score_numTopRules = num; 
}

std::unordered_map<int,std::unordered_map<int, NodeToPredRules>>& ApplicationHandler::getHeadQcandsRules(){
    return headQcandsRules;
}
std::unordered_map<int,std::unordered_map<int, CandidateConfs>>&  ApplicationHandler::getHeadQcandsConfs(){
    return headQcandsConfs;
}

std::unordered_map<int,std::unordered_map<int, NodeToPredRules>>&  ApplicationHandler::getTailQcandsRules(){
    return tailQcandsRules;
}
std::unordered_map<int,std::unordered_map<int, CandidateConfs>>& ApplicationHandler::getTailQcandsConfs(){
    return tailQcandsConfs;
}

std::vector<std::array<double, 4>>& ApplicationHandler::getTripleScores(){
    return tripleScores;
}
std::vector<std::pair<Triple, RuleGroundings>>& ApplicationHandler::getTripleGroundings(){
    return tripleGroundings;
}

void ApplicationHandler::setNumThr(int num){
    if (num==-1){
        num_thr = omp_get_max_threads();
    }else{
        num_thr = num;
    }
}

void ApplicationHandler::setAdaptTopK(bool ind){
    adapt_topk = ind;
}