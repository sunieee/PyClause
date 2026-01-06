#ifndef COMBO_H
#define COMBO_H

#include <vector>
#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>

// Forward declaration
class Rule;
class RuleStorage;

// Combo class represents a combination of 2-3 rules with a combined confidence
// Uses hash-based identification to avoid dependency on Rule objects
class Combo {
public:
    int length;                        // Number of member rules (2 or 3) - branch
    int depth;                         // Max body atoms across members
    bool isBinary;                     // True if all member rules are binary (B-type)
    size_t hashCode;                   // Hash computed from memberHashes
    int numTrue;                       // Number of correct predictions
    int numPreds;                      // Total number of predictions
    int numUnseen;                     // Laplace smoothing parameter
    std::vector<size_t> ruleHashes;    // Hashes of member rules

    Combo(const std::vector<size_t>& ruleHashes, int numTrue, int numPreds, bool isBinary);
    
    // Get confidence with Laplace smoothing: numTrue / (numPreds + numUnseen)
    double getConfidence() const;
    
    // Get surprisal: -ln(1-confidence)
    double getSurprisal() const;
    
    // Get surprisal lift: combo surprisal - sum of member rule surprisals
    // Requires RuleStorage to look up member rules by hash
    double getSurprisalLift(RuleStorage* ruleStorage);
    
    // Set the Laplace smoothing parameter
    void setNumUnseen(int val);
    
    // Set depth (max body atoms across members)
    void setDepth(int val);
    
    // Get depth
    int getDepth() const { return depth; }
    
    // Compute hash from sorted rule hashes
    void computeHash(const std::vector<size_t>& sortedHashes);

private:
    // Cached surprisal lift value
    mutable double cachedSurprisalLift;
    mutable bool surprisalLiftCached;
};

#endif // COMBO_H
