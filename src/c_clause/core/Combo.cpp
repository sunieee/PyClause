#include "Combo.h"
#include "Globals.h"
#include <iostream>
#include <algorithm>

Combo::Combo(const std::vector<size_t>& ruleHashes, int numTrue, int numPreds, bool isBinary) 
    : numTrue(numTrue), numPreds(numPreds), isBinary(isBinary) {
    
    // Create a sorted copy for hash computation
    std::vector<size_t> sortedHashes = ruleHashes;
    std::sort(sortedHashes.begin(), sortedHashes.end());
    
    length = sortedHashes.size();
    confidence = (numPreds > 0) ? (double)numTrue / numPreds : 0.0;
    
    computeHash(sortedHashes);
    
    if (comboDebug) {
        std::cout << "[Combo] Created with " << length << " rules, conf=" << confidence << std::endl;
    }
}

void Combo::computeHash(const std::vector<size_t>& sortedHashes) {
    hashCode = 0;
    for (size_t ruleHash : sortedHashes) {
        // Combine hashes using XOR and bit rotation for good distribution
        hashCode ^= ruleHash + 0x9e3779b9 + (hashCode << 6) + (hashCode >> 2);
    }
}
