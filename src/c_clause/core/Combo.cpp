#include "Combo.h"
#include "Globals.h"
#include "RuleStorage.h"
#include "Rule.h"
#include <iostream>
#include <algorithm>
#include <cmath>

Combo::Combo(const std::vector<size_t>& ruleHashes, int numTrue, int numPreds, bool isBinary) 
    : numTrue(numTrue), numPreds(numPreds), isBinary(isBinary), numUnseen(0), depth(0),
      ruleHashes(ruleHashes), cachedSurprisalLift(0.0), surprisalLiftCached(false) {
    
    // Create a sorted copy for hash computation
    std::vector<size_t> sortedHashes = ruleHashes;
    std::sort(sortedHashes.begin(), sortedHashes.end());
    
    length = sortedHashes.size();
    
    computeHash(sortedHashes);
    
    if (comboDebug) {
        std::cout << "[Combo] Created with " << length << " rules, conf=" << numTrue << "/" << numPreds << std::endl;
    }
}

double Combo::getConfidence() const {
    // Apply Laplace smoothing: numTrue / (numPreds + numUnseen)
    return (double)numTrue / ((double)numPreds + (double)numUnseen);
}

double Combo::getSurprisal() const {
    double confidence = getConfidence();
    return -std::log(1.0 - confidence);
}

double Combo::getSurprisalLift(RuleStorage* ruleStorage) {
    // Return cached value if available
    if (surprisalLiftCached) {
        return cachedSurprisalLift;
    }
    
    // Compute surprisal lift
    double comboSurprisal = getSurprisal();
    double memberSurprisalSum = 0.0;
    
    auto& hashToRule = ruleStorage->hashToRule;
    
    for (size_t ruleHash : ruleHashes) {
        auto it = hashToRule.find(ruleHash);
        if (it != hashToRule.end()) {
            Rule* rule = it->second;
            memberSurprisalSum += rule->getSurprisal();
        }
    }
    
    cachedSurprisalLift = comboSurprisal - memberSurprisalSum;
    surprisalLiftCached = true;
    
    return cachedSurprisalLift;
}

void Combo::setNumUnseen(int val) {
    numUnseen = val;
    // Invalidate cache when numUnseen changes
    surprisalLiftCached = false;
}

void Combo::setDepth(int val) {
    depth = val;
}

void Combo::computeHash(const std::vector<size_t>& sortedHashes) {
    hashCode = 0;
    for (size_t ruleHash : sortedHashes) {
        // Combine hashes using XOR and bit rotation for good distribution
        hashCode ^= ruleHash + 0x9e3779b9 + (hashCode << 6) + (hashCode >> 2);
    }
}
