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
    // expects a file with lines val\tval\t\val\trulestring
    //if sampled the first 3 values in a ruleset refer to sampled values
    void readAnyTimeFormat(std::string path, bool exact); 
    void readAnyTimeFromVec(std::vector<std::string>& stringLines, bool exact);
    void readAnyTimeFromVecs(std::vector<std::string>& ruleStrings, std::vector<std::pair<int,int>> stats, bool exact); 

    // ruleLine is num_pred /t support /t conf /t ruleString
    bool addAnyTimeRuleLine(std::string ruleLine, int id, bool exact);

    void readAnyTimeParFormat(std::string path, bool exact, int numThreads);

    bool addAnyTimeRuleWithStats(std::string ruleString, int id, int numPred, int numTrue, bool exact);
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