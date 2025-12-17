#ifndef COMBO_H
#define COMBO_H

#include <vector>
#include <algorithm>
#include "Rule.h"

// Forward declaration
class Rule;

// Combo class represents a combination of 2-3 rules with a combined confidence
class Combo {
public:
    std::vector<Rule*> memberRules;  // Sorted by rule ID
    int length;                       // Number of member rules (2 or 3)
    long long hashCode;               // Hash of rule IDs
    int numTrue;                      // Number of correct predictions
    int numPreds;                     // Total number of predictions
    double confidence;                // Confidence = numTrue / numPreds

    Combo(std::vector<Rule*>& rules, int numTrue, int numPreds);
    
    // Compute hash from sorted rule IDs
    void computeHash();
};

#endif // COMBO_H
