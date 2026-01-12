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

// Helper function to calculate surprisal for a group of rules
double ApplicationHandler::calculateGroupSurprisal(
    const std::vector<int>& groupIndices,
    const std::vector<Rule*>& allRules,
    const std::vector<Combo*>& allFulfilledCombos,
    const std::unordered_map<size_t, int>& hashToIndex,
    bool shouldDebug,
    TripleStorage& data,
    const std::string& groupType
) {
    if (groupIndices.empty()) return 0.0;
    
    // Create a set for quick lookup
    std::unordered_set<int> groupSet(groupIndices.begin(), groupIndices.end());
    
    // Extract positive and negative edges within this group
    struct Edge {
        int i, j;
        double lift;
    };
    std::vector<Edge> positiveEdges, negativeEdges;
    
    for (Combo* combo : allFulfilledCombos) {
        auto it1 = hashToIndex.find(combo->ruleHash1);
        auto it2 = hashToIndex.find(combo->ruleHash2);
        if (it1 == hashToIndex.end() || it2 == hashToIndex.end()) continue;
        
        int idx1 = it1->second;
        int idx2 = it2->second;
        
        // Only include edges where both endpoints are in this group
        if (groupSet.count(idx1) && groupSet.count(idx2)) {
            Edge edge{idx1, idx2, combo->getSurprisalLift()};
            if (edge.lift > 0) {
                positiveEdges.push_back(edge);
            } else if (edge.lift < 0) {
                negativeEdges.push_back(edge);
            }
        }
    }
    
    // Step 1: Calculate adjusted surprisal with negative edge suppression
    double negativeWeight = comboHandler.getNegativeWeight();
    std::unordered_map<int, int> ruleInDegree;
    std::unordered_map<int, double> adjustedSurprisal;
    double maxAdjustedSurprisal = 0.0;
    
    for (int idx : groupIndices) {
        ruleInDegree[idx] = 0;
    }
    
    for (const Edge& edge : negativeEdges) {
        double w_i = allRules[edge.i]->getSurprisal();
        double w_j = allRules[edge.j]->getSurprisal();
        if (w_i >= w_j) {
            ruleInDegree[edge.j]++;
        } else {
            ruleInDegree[edge.i]++;
        }
    }
    
    for (int idx : groupIndices) {
        double originalSurprisal = allRules[idx]->getSurprisal();
        double suppression = 1.0 + negativeWeight * ruleInDegree[idx];
        adjustedSurprisal[idx] = originalSurprisal / suppression;
        maxAdjustedSurprisal = std::max(maxAdjustedSurprisal, adjustedSurprisal[idx]);
    }
    
    // Step 2: Calculate sharpness factors (alpha)
    double aggregateSharpness = comboHandler.getAggregateSharpness();
    std::unordered_map<int, double> alphaFactors;
    
    if (maxAdjustedSurprisal > 0) {
        for (int idx : groupIndices) {
            double ratio = adjustedSurprisal[idx] / maxAdjustedSurprisal;
            alphaFactors[idx] = std::pow(ratio, aggregateSharpness);
        }
    } else {
        for (int idx : groupIndices) {
            alphaFactors[idx] = 1.0;
        }
    }
    
    // Step 3: Apply positive method and calculate synergy
    double addedSynergy = 0.0;
    std::string positiveMethod = comboHandler.getPositiveMethod();
    
    if (positiveMethod != "none" && !positiveEdges.empty()) {
        std::vector<Edge> selectedEdges;
        
        if (positiveMethod == "mst") {
            // Maximum Spanning Tree
            std::sort(positiveEdges.begin(), positiveEdges.end(),
                [](const Edge& a, const Edge& b) { return a.lift > b.lift; });
            
            std::unordered_map<int, int> parent;
            for (int idx : groupIndices) parent[idx] = idx;
            
            std::function<int(int)> find = [&](int x) -> int {
                if (parent[x] != x) parent[x] = find(parent[x]);
                return parent[x];
            };
            
            for (const Edge& edge : positiveEdges) {
                int px = find(edge.i), py = find(edge.j);
                if (px != py) {
                    parent[px] = py;
                    selectedEdges.push_back(edge);
                    double minAlpha = std::min(alphaFactors[edge.i], alphaFactors[edge.j]);
                    addedSynergy += edge.lift * minAlpha;
                }
            }
        } else if (positiveMethod == "matching1" || positiveMethod == "matching2") {
            // b-Matching
            int b = (positiveMethod == "matching1") ? 1 : 2;
            std::sort(positiveEdges.begin(), positiveEdges.end(),
                [](const Edge& a, const Edge& b) { return a.lift > b.lift; });
            
            std::unordered_map<int, int> nodeDegree;
            for (int idx : groupIndices) nodeDegree[idx] = 0;
            
            for (const Edge& edge : positiveEdges) {
                if (nodeDegree[edge.i] < b && nodeDegree[edge.j] < b) {
                    selectedEdges.push_back(edge);
                    nodeDegree[edge.i]++;
                    nodeDegree[edge.j]++;
                    double minAlpha = std::min(alphaFactors[edge.i], alphaFactors[edge.j]);
                    addedSynergy += edge.lift * minAlpha;
                }
            }
        } else if (positiveMethod == "all") {
            // Use all positive edges
            for (const Edge& edge : positiveEdges) {
                selectedEdges.push_back(edge);
                double minAlpha = std::min(alphaFactors[edge.i], alphaFactors[edge.j]);
                addedSynergy += edge.lift * minAlpha;
            }
        }
        
        if (shouldDebug && !selectedEdges.empty()) {
            std::cout << "      [" << groupType << " group] Selected " << selectedEdges.size() 
                      << " positive edges, synergy=" << addedSynergy << std::endl;
        }
    }
    
    // Step 4: Calculate final group surprisal
    double weightedBaseSurprisal = 0.0;
    for (int idx : groupIndices) {
        weightedBaseSurprisal += adjustedSurprisal[idx] * alphaFactors[idx];
    }
    
    double groupSurprisal = weightedBaseSurprisal + comboHandler.getPositiveWeight() * addedSynergy;
    
    if (shouldDebug && groupIndices.size() > 0) {
        std::cout << "      [" << groupType << " group] " << groupIndices.size() 
                  << " rules, base=" << weightedBaseSurprisal 
                  << ", synergy_term=" << (comboHandler.getPositiveWeight() * addedSynergy)
                  << ", total=" << groupSurprisal << std::endl;
    }
    
    return groupSurprisal;
}

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
    std::cout << "  Combo Positive Method: " << comboHandler.getPositiveMethod() << std::endl;
    std::cout << "  Combo Positive Weight: " << comboHandler.getPositiveWeight() << std::endl;
    std::cout << "  Combo Negative Weight: " << comboHandler.getNegativeWeight() << std::endl;
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
    bool hasPositiveMethod = (comboHandler.getPositiveMethod() != "none");
    double negativeWeight = comboHandler.getNegativeWeight();
    
    if (!hasPositiveMethod && negativeWeight == 0.0) {
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
        std::unordered_map<size_t, int> hashToIndex;
        for (size_t i = 0; i < appliedRules.size(); i++) {
            hashToIndex[appliedRules[i]->getRuleHash()] = i;
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
        
        if (shouldDebug) {
            std::cout << "\n  Candidate " << candidate << " (" 
                      << data.getIndex()->getStringOfNodeId(candidate) << "):" << std::endl;
            std::cout << "    Applied Rules: " << appliedRules.size() << std::endl;
            std::cout << "    Fulfilled Combos: " << fulfilledCombos.size() << std::endl;
        }
        
        double newSurprisal = 0.0;
        
        // ========== Check if_grouping parameter ==========
        if (comboHandler.getIfGrouping()) {
            // Group rules into two types: unary and binary
            // Binary: only type "b" rules
            // Unary: all other rules (types "c", "d", "z", "xxc", "xxd")
            std::vector<int> unaryIndices, binaryIndices;
            
            for (size_t i = 0; i < appliedRules.size(); i++) {
                Rule* rule = appliedRules[i];
                std::string rType(rule->type);
                
                if (rType == "b") {
                    // Binary rules: type "b" only
                    binaryIndices.push_back(i);
                } else {
                    // Unary rules: all other types ("c", "d", "z", "xxc", "xxd")
                    unaryIndices.push_back(i);
                }
            }
            
            if (shouldDebug) {
                std::cout << "    [GROUPING] Unary: " << unaryIndices.size()
                          << ", Binary: " << binaryIndices.size() << std::endl;
            }
            
            // Calculate surprisal for each group
            double unarySurprisal = calculateGroupSurprisal(
                unaryIndices, appliedRules, fulfilledCombos, hashToIndex,
                shouldDebug, data, "unary"
            );
            
            double binarySurprisal = calculateGroupSurprisal(
                binaryIndices, appliedRules, fulfilledCombos, hashToIndex,
                shouldDebug, data, "binary"
            );
            
            // Combine with weights (unary weight is fixed at 1.0)
            double binaryWeight = comboHandler.getBinaryWeight();
            
            newSurprisal = unarySurprisal + binaryWeight * binarySurprisal;
            
            if (shouldDebug) {
                std::cout << "    Final weighted combination:" << std::endl;
                std::cout << "      Unary: 1.0 * " << unarySurprisal 
                          << " = " << unarySurprisal << std::endl;
                std::cout << "      Binary: " << binaryWeight << " * " << binarySurprisal 
                          << " = " << (binaryWeight * binarySurprisal) << std::endl;
                std::cout << "      Total surprisal: " << newSurprisal << std::endl;
            }
        } else {
            // No grouping: treat all rules together
            std::vector<int> allIndices;
            for (size_t i = 0; i < appliedRules.size(); i++) {
                allIndices.push_back(i);
            }
            
            newSurprisal = calculateGroupSurprisal(
                allIndices, appliedRules, fulfilledCombos, hashToIndex,
                shouldDebug, data, "all_rules"
            );
        }
        
        aggregatedSurprisal[candidate] = newSurprisal;
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
                std::cout << "  Combo Positive Method: " << comboHandler.getPositiveMethod() << std::endl;
                std::cout << "  Combo Positive Weight: " << comboHandler.getPositiveWeight() << std::endl;
                std::cout << "  Combo Negative Weight: " << comboHandler.getNegativeWeight() << std::endl;
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
    
    // for noisy-or we can simply sort according to aggrCand after scoring
    // here we have to sort and score separately
    std::vector<std::pair<int, std::vector<Rule*>>> candsToSort(candToRules.begin(), candToRules.end());

    // max+ sorting
    auto sortLexicographic = [&train, this](const std::pair<int, std::vector<Rule*>>& candA, const std::pair<int, std::vector<Rule*>>& candB) { 
        std::vector<Rule*> rulesA = candA.second;
        std::vector<Rule*> rulesB = candB.second;

        int minRules = std::min(rulesA.size(), rulesB.size());
        for (int i=0; i<minRules; i++){
            double confA = rulesA[i]->getConfidence();
            double confB = rulesB[i]->getConfidence();
            if (confA > confB){
                return true;
            } else if (confB > confA){
                return false;
            }
        }
        // all compared rules were equal rank according to num rules
        if (rulesB.size() > rulesA.size()){
            return false;
        } else if (rulesA.size() > rulesB.size()){
            return true;
        }
        //exactly the same rules given NodeToPred is unordered_map return random order
        if (this->rank_tie_handling=="random"){
            return false;
        }else if (this->rank_tie_handling=="frequency"){
            if (train.getFreq(candA.first)!=train.getFreq(candB.first)){
                return train.getFreq(candA.first) > train.getFreq(candB.first);
            }else{
                return candA.first<candB.first;
            }
            
        } else {
            throw std::runtime_error("Could not understand tie_handling_paramter in scoreMaxPlus.");
        }
    };
     std::sort(candsToSort.begin(), candsToSort.end(), sortLexicographic);
    

    // take sorted candidate and derive its score according to highest rule
     for (auto& pair: candsToSort){
        aggrCand.push_back(
            std::make_pair(
                pair.first,
                pair.second[0]->getConfidence()
                )
            );
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