#ifndef RULESTORAGE_H
#define RULESTORAGE_H

#include "Rule.h"
#include "Combo.h"
#include "Globals.h"
#include "Types.h"
#include "RuleFactory.h"

#include <vector>
#include <memory>
#include <mutex>

class RuleStorage
{

public:
    RuleStorage(std::shared_ptr<Index> index, std::shared_ptr<RuleFactory> ruleFactory);
    
    // Main loading function - expects a file with lines: numPred\tnumTrue\tconf\trulestring
    void readAnyTimeParFormat(std::string path, bool exact, int numThreads);
    
    // Deprecated functions - throw not implemented error
    void readAnyTimeFormat(std::string path, bool exact) {
        throw std::runtime_error("readAnyTimeFormat is deprecated. Please use readAnyTimeParFormat instead.");
    }
    
    void readAnyTimeFromVec(std::vector<std::string>& stringLines, bool exact) {
        throw std::runtime_error("readAnyTimeFromVec is deprecated. Please use readAnyTimeParFormat instead.");
    }
    
    void readAnyTimeFromVecs(std::vector<std::string>& ruleStrings, std::vector<std::pair<int,int>> stats, bool exact) {
        throw std::runtime_error("readAnyTimeFromVecs is deprecated. Please use readAnyTimeParFormat instead.");
    }
    
    bool addAnyTimeRuleLine(std::string ruleLine, int id, bool exact) {
        throw std::runtime_error("addAnyTimeRuleLine is deprecated and not implemented.");
    }
    
    bool addAnyTimeRuleWithStats(std::string ruleString, int id, int numPred, int numTrue, bool exact) {
        throw std::runtime_error("addAnyTimeRuleWithStats is deprecated and not implemented.");
    }

    std::vector<std::unique_ptr<Rule>>& getRules();
    std::unordered_map<int, std::set<Rule*,compareRule>>& getRelToRules();
    std::set<Rule*, compareRule>& getRelRules(int relation);
    void clearAll();
    
    // Combo-related methods
    void addCombo(std::unique_ptr<Combo> combo);
    void addToComboIndex(size_t ruleHash, Combo* combo);
    std::unordered_map<size_t, std::vector<Combo*>>& getRuleHashToCombos() { return ruleHashToCombos; }
    bool hasCombos() const { return !combos.empty(); }
    
    // Print loading statistics
    void printStatistics();
    
private:
    // rules owns the rule objects
    // RelToRules keeps the rules sorted due to the set, iterating over all the rules would
    // rules defines the global index but should never be used for application features
    // e.g. from python side user may want to subset rules which will only affect relToRules
    // as we need to be able to come back to larger sets after subsetting
    std::vector<std::unique_ptr<Rule>> rules;
    // from here rule application is performed; application is always based on a target relation
    std::unordered_map<int, std::set<Rule*,compareRule>> relToRules;
    std::shared_ptr<Index> index;
    // TODO you dont really need a shared pointer here; but at least options should be global 
    std::shared_ptr<RuleFactory> ruleFactory;

    bool verbose = true;
    
    // Combo storage: owns all combo objects
    std::vector<std::unique_ptr<Combo>> combos;
    // Inverted index: ruleHash -> combos containing this rule
    std::unordered_map<size_t, std::vector<Combo*>> ruleHashToCombos;
    // Debug map: ruleHash -> rule
    std::unordered_map<size_t, Rule*> hashToRule;
    // Mutex for thread-safe combo index updates
    std::mutex comboIndexMutex;

};

#endif // RULESTORAGE_H