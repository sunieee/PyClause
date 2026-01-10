#ifndef COMBO_H
#define COMBO_H

#include <vector>
#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>
#include <iostream>

// Forward declaration
class Rule;
class RuleStorage;

// Include Globals.h for comboDebug
#include "Globals.h"

// Combo class represents a combination of 2 rules with a combined confidence
// Uses hash-based identification to avoid dependency on Rule objects
class Combo {
public:
    int length;                        // Number of member rules (always 2)
    int depth;                         // Max body atoms across members
    bool isBinary;                     // True if all member rules are binary (B-type)
    size_t hashCode;                   // Hash computed from memberHashes
    int numTrue;                       // Number of correct predictions
    int numPreds;                      // Total number of predictions
    int numUnseen;                     // Laplace smoothing parameter
    size_t ruleHash1;                  // Hash of first member rule
    size_t ruleHash2;                  // Hash of second member rule
    double lift;                       // Surprisal lift value (from third column in rule file)

    Combo(size_t ruleHash1, size_t ruleHash2, int numTrue, int numPreds, bool isBinary, double lift) 
        : numTrue(numTrue), numPreds(numPreds), isBinary(isBinary), numUnseen(1), depth(0),
          ruleHash1(ruleHash1), ruleHash2(ruleHash2), lift(lift) {
        
        // Always 2 rules
        length = 2;
        
        computeHash();
        
        if (comboDebug) {
            std::cout << "[Combo] Created with 2 rules, conf=" << numTrue << "/" << numPreds << ", lift=" << lift << std::endl;
        }
    }
    
    // Get confidence with Laplace smoothing: numTrue / (numPreds + numUnseen)
    double getConfidence() const {
        // Apply Laplace smoothing: numTrue / (numPreds + numUnseen)
        return (double)numTrue / ((double)numPreds + (double)numUnseen);
    }
    
    // Get surprisal: -ln(1-confidence)
    double getSurprisal() const {
        double confidence = getConfidence();
        return -std::log(1.0 - confidence);
    }
    
    // Get surprisal lift: returns the stored lift value directly
    double getSurprisalLift() const {
        // Return the stored lift value directly (from third column in rule file)
        return lift;
    }
    
    // Set depth (max body atoms across members)
    void setDepth(int val) {
        depth = val;
    }
    
    // Get depth
    int getDepth() const { return depth; }
    
    // Compute hash from two rule hashes
    void computeHash() {
        // Sort the two hashes for consistent hash computation
        size_t h1 = std::min(ruleHash1, ruleHash2);
        size_t h2 = std::max(ruleHash1, ruleHash2);
        
        // Combine hashes using XOR and bit rotation for good distribution
        hashCode = 0;
        hashCode ^= h1 + 0x9e3779b9 + (hashCode << 6) + (hashCode >> 2);
        hashCode ^= h2 + 0x9e3779b9 + (hashCode << 6) + (hashCode >> 2);
    }
    
    // Get rule hashes as vector (for compatibility)
    std::vector<size_t> getRuleHashes() const { return {ruleHash1, ruleHash2}; }
};

#endif // COMBO_H
