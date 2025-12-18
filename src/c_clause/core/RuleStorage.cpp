#include "RuleStorage.h"
#include "Index.h"
#include "Globals.h"
#include "Rule.h"
#include "Combo.h"
#include "Types.h"

#include <fstream>
#include <string>
#include <iomanip>
#include <unordered_set>
#include <cctype>


RuleStorage::RuleStorage(std::shared_ptr<Index> index, std::shared_ptr<RuleFactory> ruleFactory){
    this->ruleFactory = ruleFactory;
    this->index = index;
    // Set RuleStorage pointer in RuleFactory for combo parsing
    this->ruleFactory->setRuleStorage(this);
}


void RuleStorage::readAnyTimeParFormat(std::string path, bool exact, int numThreads){
     std::ifstream file(path);
    if (!file.is_open()) {
        throw std::ios_base::failure("Could not open rule file: " + path + " is the path correct?");
    }

    if (verbose){
        std::cout << "Loading rules from " + path << std::endl;
    }

    std::ios_base::sync_with_stdio(false); 
    
    constexpr size_t bufferSize =  256 * 1024; 
    char buffer[bufferSize];
    file.rdbuf()->pubsetbuf(buffer, bufferSize);

    std::string line;
    int currLine = 0;

    std::vector<std::string> ruleLines;
    std::vector<std::unique_ptr<Rule>> rules_ptr;


    while (!util::safeGetline(file, line).eof()){
        ruleLines.push_back(line);
    }
    file.close();

    // we not need all of them 
    rules_ptr.resize(ruleLines.size());
    std::vector<std::string> ruleStrings_vec(ruleLines.size());
    std::vector<size_t> ruleHashes_vec(ruleLines.size());

    #pragma omp parallel num_threads(numThreads)
    {
        #pragma omp for //no need for dynamic
        for (int i=0; i<ruleLines.size(); i++){

            if (i>0 && i%1000000==0){
                std::cout<<"parsed 1 million rules..." <<std::endl;
            }

            std::string ruleLine = ruleLines[i];

            // expects a line: predicted\t cpredicted\tconf\trulestring
            std::vector<std::string> splitline = util::split(ruleLine, '\t');

            if (splitline.size()==1){
                int numPreds = 100;
                int numTrue = 100;
                std::cout<<"Warning: could not find num preds and support for input line " + splitline[0]<<std::endl;
                std::cout<<" Setting both to 100. Expect random ordering for rules and predictions, confidence scores will all be 1."<<std::endl;
            }

            if (splitline.size()!=4){
                std::cout<<"Could not parse this rule because of line format: " + ruleLine<<std::endl;
                std::cout<<"Skipping but please check your format."<<std::endl;

            }
            std::string ruleString = splitline[3];
            int numPreds = std::stoi(splitline[0]);
            int numTrue = std::stoi(splitline[1]);
            
            // Store rule string for later collection
            ruleStrings_vec[i] = ruleString;
            
            // Compute hash for the rule string
            std::hash<std::string> hasher;
            size_t ruleHash = hasher(ruleString);
            ruleHashes_vec[i] = ruleHash;
            
            std::unique_ptr<Rule> rule = nullptr;
            try
                {
                    rule = ruleFactory->parseAnytimeRule(ruleString, numPreds, numTrue);
                }
            catch(const std::exception& e)
                {
                    std::cout<<"Could not parse this rule " + ruleLine<<std::endl;
                    std::cout<<"[RuleStorage] Exception details: " << e.what() << std::endl;
                    std::cout<<"Skipping it, but please check your format."<<std::endl;
                    std::cout<<"And check that if entities are contained, that they are loaded with the data."<<std::endl;
                }

            if (rule){
                rule->setStats(numPreds, numTrue, exact);
                rule->setRuleHash(ruleHash);
                rules_ptr.at(i) = std::move(rule);              
            }
        }
    }
    // need this for correctly setting ID's
    // e.g. we want ID's to be the line order (minus skipped)
    // to be consistent when rules are written and loaded again
    std::cout<< "Indexing rules.." <<std::endl;
    int currID = 0;
    std::unordered_set<size_t> seenHashes;
    
    for (int i=0; i<rules_ptr.size(); i++){
        if (rules_ptr[i]){
            size_t ruleHash = ruleHashes_vec[i];
            
            // Check for duplicate rules using hash
            if (seenHashes.find(ruleHash) != seenHashes.end()) {
                std::cerr << "ERROR: Duplicate rule detected!" << std::endl;
                std::cerr << "Rule ID: " << currID << std::endl;
                std::cerr << "Rule: " << ruleStrings_vec[i] << std::endl;
                throw std::runtime_error("Duplicate rule in RuleStorage");
            }
            seenHashes.insert(ruleHash);
            
            rules_ptr[i]->setID(currID);
            
            // Store for debugging: hash to rule mapping
            hashToRule[ruleHash] = rules_ptr[i].get();
            
            // must be done after id is set
            relToRules[rules_ptr[i]->getTargetRel()].insert(rules_ptr[i].get());
            currID += 1;
            rules.push_back(std::move(rules_ptr[i]));
        }
    }
    std::cout<<"Loaded and indexed "<<currID<<" rules."<<std::endl;
    printStatistics();
}

std::set<Rule*, compareRule>& RuleStorage::getRelRules(int relation){
    return relToRules[relation];
}

std::unordered_map<int, std::set<Rule*,compareRule>>& RuleStorage::getRelToRules(){
    return relToRules;
}

std::vector<std::unique_ptr<Rule>>& RuleStorage::getRules(){
    return rules;
 }

void RuleStorage::addCombo(std::unique_ptr<Combo> combo) {
    Combo* comboPtr = combo.get();
    
    // Add to storage first
    combos.push_back(std::move(combo));
}

void RuleStorage::addToComboIndex(size_t ruleHash, Combo* combo) {
    std::lock_guard<std::mutex> lock(comboIndexMutex);
    ruleHashToCombos[ruleHash].push_back(combo);
}

void RuleStorage::clearAll(){
    rules.clear();
    relToRules.clear();
    combos.clear();
    ruleHashToCombos.clear();
}

void RuleStorage::printStatistics() {
    // Count rules by type
    std::unordered_map<std::string, int> ruleTypeCounts;
    for (const auto& rule : rules) {
        std::string type(rule->type);
        ruleTypeCounts[type]++;
    }
    
    std::cout << "\n========== Rule Loading Statistics ==========" << std::endl;
    std::cout << "Regular Rules:" << std::endl;
    std::cout << "  Total: " << rules.size() << std::endl;
    for (const auto& pair : ruleTypeCounts) {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
    }
    
    // Count combos by length and type
    if (!combos.empty()) {
        int len2_unary = 0, len2_binary = 0;
        int len3_unary = 0, len3_binary = 0;
        
        for (const auto& combo : combos) {
            if (combo->length == 2) {
                if (combo->isBinary) len2_binary++;
                else len2_unary++;
            } else if (combo->length == 3) {
                if (combo->isBinary) len3_binary++;
                else len3_unary++;
            }
        }
        
        std::cout << "\nCombo Rules:" << std::endl;
        std::cout << "  Total: " << combos.size() << std::endl;
        std::cout << "\n  Distribution (Length × Type):" << std::endl;
        std::cout << "                Unary    Binary" << std::endl;
        std::cout << "    Length 2:  " << std::setw(6) << len2_unary << "   " << std::setw(6) << len2_binary << std::endl;
        std::cout << "    Length 3:  " << std::setw(6) << len3_unary << "   " << std::setw(6) << len3_binary << std::endl;
        
        // Check if all rule hashes in combos have corresponding rules
        std::cout << "\n  Validating combo rule references..." << std::endl;
        std::unordered_map<size_t, std::string> allRuleHashes;
        for (const auto& rule : rules) {
            size_t hash = rule->getRuleHash();
            std::string ruleStr = rule->computeRuleString(index.get());
            allRuleHashes[hash] = ruleStr;
        }
        
        std::unordered_set<size_t> missingHashes;
        int totalReferences = 0;
        for (const auto& pair : ruleHashToCombos) {
            totalReferences++;
            if (allRuleHashes.find(pair.first) == allRuleHashes.end()) {
                missingHashes.insert(pair.first);
            }
        }
        
        if (missingHashes.empty()) {
            std::cout << "  ✓ All " << totalReferences << " rule references are valid" << std::endl;
        } else {
            std::cout << "  ⚠ WARNING: Found " << missingHashes.size() << " missing rule references!" << std::endl;
            std::cout << "\n  Sample of missing rules (first 5):" << std::endl;
            int count = 0;
            for (size_t hash : missingHashes) {
                if (count++ < 5) {
                    std::cout << "    Hash " << hash << ":" << std::endl;
                    auto rule = hashToRule.find(hash);
                    if (rule != hashToRule.end()) {
                        std::cout << "      computeRuleString: " << rule->second->computeRuleString(index.get()) << std::endl;
                        std::cout << "      haser(computeRuleString): " << std::hash<std::string>{}(rule->second->computeRuleString(index.get())) << std::endl;
                        std::cout << "      ruleString: " << rule->second->rulestring << std::endl;
                        std::cout << "      haser(ruleString): " << std::hash<std::string>{}(rule->second->rulestring) << std::endl;

                    }
                } else {
                    std::cout << "    ... (and " << (missingHashes.size() - 5) << " more)" << std::endl;
                    break;
                }
            }
        }
    }
    
    std::cout << "============================================\n" << std::endl;
}