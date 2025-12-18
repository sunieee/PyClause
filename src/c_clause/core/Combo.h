#ifndef COMBO_H
#define COMBO_H

#include <vector>
#include <algorithm>
#include <functional>

// Combo class represents a combination of 2-3 rules with a combined confidence
// Uses hash-based identification to avoid dependency on Rule objects
class Combo {
public:
    int length;                        // Number of member rules (2 or 3)
    bool isBinary;                     // True if all member rules are binary (B-type)
    size_t hashCode;                   // Hash computed from memberHashes
    int numTrue;                       // Number of correct predictions
    int numPreds;                      // Total number of predictions
    double confidence;                 // Confidence = numTrue / numPreds

    Combo(const std::vector<size_t>& ruleHashes, int numTrue, int numPreds, bool isBinary);
    
    // Compute hash from sorted rule hashes
    void computeHash(const std::vector<size_t>& sortedHashes);
};

#endif // COMBO_H
