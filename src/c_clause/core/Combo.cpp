#include "Combo.h"
#include "Globals.h"
#include <iostream>
#include <algorithm>

Combo::Combo(const std::vector<size_t>& ruleHashes, int numTrue, int numPreds, bool isBinary) 
    : numTrue(numTrue), numPreds(numPreds), isBinary(isBinary), numUnseen(0) {
    
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

void Combo::setNumUnseen(int val) {
    numUnseen = val;
}

void Combo::computeHash(const std::vector<size_t>& sortedHashes) {
    hashCode = 0;
    for (size_t ruleHash : sortedHashes) {
        // Combine hashes using XOR and bit rotation for good distribution
        hashCode ^= ruleHash + 0x9e3779b9 + (hashCode << 6) + (hashCode >> 2);
    }
}
