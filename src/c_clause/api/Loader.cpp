#include "Loader.h"

#include <functional>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>


 Loader::Loader(std::map<std::string, std::string> options){
    auto verb = options.find("verbose");
    if (verb!=options.end()){
        this->verbose = util::stringToBool(verb->second);
    }

    index = std::make_shared<Index>();
    data = std::make_unique<TripleStorage>(index);
    target = std::make_unique<TripleStorage>(index);
    filter = std::make_unique<TripleStorage>(index);
    ruleFactory = std::make_shared<RuleFactory>(index);
    setRuleOptions(options, *ruleFactory);
    rules = std::make_unique<RuleStorage>(index, ruleFactory);
 }

bool Loader::getLoadedData(){
    return loadedData;
}

bool Loader::getLoadedRules(){
    return loadedRules;
}


void Loader::setOptions(std::map<std::string, std::string> options){
    setRuleOptions(options, *ruleFactory);
}


void Loader::loadRules(std::string path, std::string jaccardPath){
    rules->clearAll();
    bodyHashPair2Jaccard.clear();
    if (!loadedData){
         throw std::runtime_error("Please load the data first with the the Handlers load data functionality.");
    }
    if (this->numThr==1){
        rules->readAnyTimeFormat(path, false);
    }else{
        rules->readAnyTimeParFormat(path, false, this->numThr);
    }
    loadedRules = true;
    
    // Load Jaccard similarities if path is provided
    if (!jaccardPath.empty()) {
        loadBodyJaccard(jaccardPath);
    }
}


void Loader::loadRules(std::vector<std::string> ruleStrings, std::string jaccardPath){
    rules->clearAll();
    bodyHashPair2Jaccard.clear();
    if (!loadedData){
         throw std::runtime_error("Please load the data first with the the Handlers load data functionality.");
    }
    rules->readAnyTimeFromVec(ruleStrings, false);
    loadedRules = true;
    
    // Load Jaccard similarities if path is provided
    if (!jaccardPath.empty()) {
        loadBodyJaccard(jaccardPath);
    }
}


void Loader::loadRules(std::vector<std::string> ruleStrings, std::vector<std::pair<int,int>> ruleStats, std::string jaccardPath){
    rules->clearAll();
    bodyHashPair2Jaccard.clear();
    if (!loadedData){
        throw std::runtime_error("Please load the data first with the the Handlers load data functionality.");
    }
    rules->readAnyTimeFromVecs(ruleStrings, ruleStats, false);
    loadedRules = true;
    
    // Load Jaccard similarities if path is provided
    if (!jaccardPath.empty()) {
        loadBodyJaccard(jaccardPath);
    }
}


void Loader::writeRules(std::string path){
    if (!loadedRules){
        throw std::runtime_error("You have to load rules first before you can write them.");
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        throw  std::runtime_error("Failed to create file. Please check if the paths are correct: " + path);
    }

    std::vector<std::unique_ptr<Rule>>& loadedRules = this->rules->getRules();
    for (int i=0; i<loadedRules.size(); i++){
        std::array<int, 2> stats = loadedRules[i]->getStats();
        int numPreds = stats[0];
        int numTrue = stats[1];
        double conf = (double) numTrue / (double) numPreds;
        file << numPreds << "\t" << numTrue << "\t" << conf << "\t" << loadedRules[i]->computeRuleString(index.get()) << std::endl;
    }
    std::cout<<"Written rules to:  " + path<<std::endl;
}

std::vector<std::string> Loader::getRuleLines(){
    if (!loadedRules){
        throw std::runtime_error("You have to load rules first before you can write them.");
    }

    std::vector<std::unique_ptr<Rule>>& loadedRules = this->rules->getRules();
    std::vector<std::string> ret;
    for (int i=0; i<loadedRules.size(); i++){
        std::array<int, 2> stats = loadedRules[i]->getStats();
        int numPreds = stats[0];
        int numTrue = stats[1];
        double conf = (double) numTrue / (double) numPreds;
        std::string ruleLine = "";
        ruleLine +=  std::to_string(numPreds) + "\t" + std::to_string(numTrue) + "\t" +
                     std::to_string(conf)     + "\t" + loadedRules[i]->computeRuleString(index.get());
        ret.push_back(ruleLine);
    }
    return ret;
}


std::unordered_map<std::string, int>& Loader::getNodeToIdx(){
    return index->getNodeToIdx();
}


std::unordered_map<std::string, int>& Loader::getRelationToIdx(){
    return index->getRelationToIdx();
}


void Loader::subsEntityStrings(std::map<std::string, std::string>& newNames){
        index->subsEntityStrings(newNames);

    }
void Loader::subsRelationStrings(std::map<std::string, std::string>& newNames){
        index->subsRelationStrings(newNames);
    }

void Loader::setRuleOptions(std::map<std::string, std::string> options, RuleFactory& ruleFactory){
    // rule options:  individual rule options and options of which rules to use
     struct OptionHandler {
        std::string name;
        std::function<void(std::string)> setter;
    };

    std::vector<OptionHandler> handlers = {
        // Z
        {"load_zero_rules", [&ruleFactory](std::string val) {ruleFactory.setCreateRuleZ(util::stringToBool(val));}},
        {"z_weight", [&ruleFactory](std::string val) {ruleFactory.setZconfWeight(std::stod(val));}},
        {"z_num_unseen", [&ruleFactory](std::string val) {ruleFactory.setNumUnseen(std::stoi(val), "z");}},
        {"z_min_support", [&ruleFactory](std::string val) {ruleFactory.setMinCorrect(std::stoi(val), "z");}},
        {"z_min_preds", [&ruleFactory](std::string val) {ruleFactory.setMinPred(std::stoi(val), "z");}},
        {"z_min_conf", [&ruleFactory](std::string val) {ruleFactory.setMinConf(std::stod(val), "z");}},
        // C
        {"load_u_c_rules", [&ruleFactory](std::string val) {ruleFactory.setCreateRuleC(util::stringToBool(val));}},
        {"c_num_unseen", [&ruleFactory](std::string val) {ruleFactory.setNumUnseen(std::stoi(val), "c");}},
        {"c_min_support", [&ruleFactory](std::string val) {ruleFactory.setMinCorrect(std::stoi(val), "c");}},
        {"c_min_preds", [&ruleFactory](std::string val) {ruleFactory.setMinPred(std::stoi(val), "c");}},
        {"c_min_conf", [&ruleFactory](std::string val) {ruleFactory.setMinConf(std::stod(val), "c");}},
        {"c_max_length", [&ruleFactory](std::string val) {ruleFactory.setCmaxLength(std::stoi(val));}},
        // B
        {"load_b_rules", [&ruleFactory](std::string val) {ruleFactory.setCreateRuleB(util::stringToBool(val));}},
        {"b_max_branching_factor", [&ruleFactory](std::string val) {ruleFactory.setBbranchingFactor(std::stoi(val));}},
        {"b_num_unseen", [&ruleFactory](std::string val) {ruleFactory.setNumUnseen(std::stoi(val), "b");}},
        {"b_min_support", [&ruleFactory](std::string val) {ruleFactory.setMinCorrect(std::stoi(val), "b");}},
        {"b_min_preds", [&ruleFactory](std::string val) {ruleFactory.setMinPred(std::stoi(val), "b");}},
        {"b_min_conf", [&ruleFactory](std::string val) {ruleFactory.setMinConf(std::stod(val), "b");}},
        {"b_max_length", [&ruleFactory](std::string val) {ruleFactory.setBmaxLength(std::stoi(val));}},
        // D
        {"load_u_d_rules", [&ruleFactory](std::string val) {ruleFactory.setCreateRuleD(util::stringToBool(val));}},
        {"d_weight", [&ruleFactory](std::string val) {ruleFactory.setDconfWeight(std::stod(val));}},
        {"d_max_branching_factor", [&ruleFactory](std::string val) {ruleFactory.setDbranchingFactor(std::stoi(val));}},
        {"d_num_unseen", [&ruleFactory](std::string val) {ruleFactory.setNumUnseen(std::stoi(val), "d");}},
        {"d_min_support", [&ruleFactory](std::string val) {ruleFactory.setMinCorrect(std::stoi(val), "d");}},
        {"d_min_preds", [&ruleFactory](std::string val) {ruleFactory.setMinPred(std::stoi(val), "d");}},
        {"d_min_conf", [&ruleFactory](std::string val) {ruleFactory.setMinConf(std::stod(val), "d");}},
        {"d_max_length", [&ruleFactory](std::string val) {ruleFactory.setDmaxLength(std::stoi(val));}},
        // XXc
        {"load_u_xxc_rules", [&ruleFactory](std::string val) {ruleFactory.setCreateRuleXXc(util::stringToBool(val));}},
        {"xxc_num_unseen", [&ruleFactory](std::string val) {ruleFactory.setNumUnseen(std::stoi(val), "xxc");}},
        {"xxc_min_support", [&ruleFactory](std::string val) {ruleFactory.setMinCorrect(std::stoi(val), "xxc");}},
        {"xxc_min_preds", [&ruleFactory](std::string val) {ruleFactory.setMinPred(std::stoi(val), "xxc");}},
        {"xxc_min_conf", [&ruleFactory](std::string val) {ruleFactory.setMinConf(std::stod(val), "xxc");}},
        // XXd
        {"load_u_xxd_rules", [&ruleFactory](std::string val) {ruleFactory.setCreateRuleXXd(util::stringToBool(val));}},
        {"xxd_num_unseen", [&ruleFactory](std::string val) {ruleFactory.setNumUnseen(std::stoi(val), "xxd");}},
        {"xxd_min_support", [&ruleFactory](std::string val) {ruleFactory.setMinCorrect(std::stoi(val), "xxd");}},
        {"xxd_min_preds", [&ruleFactory](std::string val) {ruleFactory.setMinPred(std::stoi(val), "xxd");}},
        {"xxd_min_conf", [&ruleFactory](std::string val) {ruleFactory.setMinConf(std::stod(val), "xxd");}},
        // Combo
        {"load_combo", [&ruleFactory](std::string val) {ruleFactory.setCreateCombo(util::stringToBool(val));}},
        {"combo_debug", [&ruleFactory](std::string val) {ruleFactory.setComboDebug(util::stringToBool(val));}},
        {"combo_min_support", [&ruleFactory](std::string val) {ruleFactory.setMinCorrect(std::stoi(val), "m");}},
        {"combo_min_pred", [&ruleFactory](std::string val) {ruleFactory.setMinPred(std::stoi(val), "m");}},
        {"combo_min_conf", [&ruleFactory](std::string val) {ruleFactory.setMinConf(std::stod(val), "m");}},
        {"combo_num_unseen", [&ruleFactory](std::string val) {ruleFactory.setNumUnseen(std::stoi(val), "combo");}},
        {"combo_max_depth", [&ruleFactory](std::string val) {ruleFactory.setComboMaxDepth(std::stoi(val));}},
        {"combo_max_branch", [&ruleFactory](std::string val) {ruleFactory.setComboMaxBranch(std::stoi(val));}},
        // other
        {"num_threads", [this](std::string val) {this->setNumThreads(std::stoi(val));}},
        
    };

    for (auto& handler : handlers) {
        auto opt = options.find(handler.name);
        if (opt != options.end()) {
            if (verbose){
                std::cout<< "Setting option "<<handler.name<<" to: "<<opt->second<<std::endl;
            }
            handler.setter(opt->second);
           
        }
    }
}

void Loader::updateRules(){
    ruleFactory->updateRules(rules->getRules(), rules->getRelToRules());
}

std::vector<std::string> Loader::getRuleIdx(){
    if (!loadedRules){
        throw std::runtime_error("You cannot obtain a rule index before you loaded rules into the laoder.");
    }
    std::vector<std::unique_ptr<Rule>>& allRules = rules->getRules();
    std::vector<std::string> out(allRules.size());
    for (int i=0; i<allRules.size(); i++){
        Rule* rule = allRules[i].get();
        if (rule->getID() != i){
            throw std::runtime_error("A rule's idx does not match its position. This is an internal; error check the backend.");
        }
        out.at(i) = rule->computeRuleString(index.get());
    }
    return out;
}


TripleStorage& Loader::getData(){
    return *data;
}

TripleStorage& Loader::getFilter(){
    return *filter;
}

TripleStorage& Loader::getTarget(){
    return *target;
}

RuleStorage& Loader::getRules(){
    return *rules;
}

RuleFactory& Loader::getRuleFactory(){
    return *ruleFactory;
}


std::shared_ptr<Index> Loader::getIndex(){
    return index;
}

// loads a file with tab separated string (token) triples
std::unique_ptr<std::vector<Triple>> Loader::loadTriplesToVec(std::string path){

    auto triples = std::make_unique<std::vector<Triple>>();

    // Open the file
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    // Read the file line by line
    std::string line;


    while (!util::safeGetline(file, line).eof()){
		std::istringstream iss(line);
		std::vector<std::string> results = util::split(line, '\t');
		if (results.size() != 3) {
			throw std::runtime_error("Error while reading a file with Triples please check that every line follows tab separeated: head relation tail format. ");
		}
		Triple triple;
        //TODO catch error here when unseen entity appears; for more informative error handling
        triple[0] = index->getIdOfNodestring(results[0]);
        triple[1] = index->getIdOfRelationstring(results[1]);
        triple[2] = index->getIdOfNodestring(results[2]);

        if (!iss.fail() || iss.eof()) {
            triples->push_back(triple);
        }else{
            throw std::runtime_error("Error while reading a file with Triples please check that every line follows tab separeated: head relation tail format. ");
        }
	}
	file.close();
    return std::move(triples);
}


void Loader::setNodeIndex(std::vector<std::string>& idxToNode){
    if (loadedData){
        throw std::runtime_error("You can only set an entity index before you loaded data.");
    }
        index->setNodeIndex(idxToNode);

}
void Loader::setRelIndex(std::vector<std::string>& idxToRel){
    if (loadedData){
        throw std::runtime_error("You can only set a relation index before you loaded data.");
    }
    index->setRelIndex(idxToRel);
}

// number of threads for parallel rule parsing (from disc)
void Loader::setNumThreads(int num){
    if (num==-1){
        // one parsing operation is cheap, to many threads create
        // too much overhead
        numThr = std::min(5, omp_get_max_threads());
    }else{
        numThr = num;
    }
}

void Loader::loadBodyJaccard(std::string filepath){
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::ios_base::failure("Could not open body Jaccard file: " + filepath);
    }
    
    bodyHashPair2Jaccard.clear();
    std::string line;
    std::hash<std::string> hasher;
    
    // Simple JSON parsing for the specific format
    // Format: "body1;body2": value,
    while (std::getline(file, line)) {
        // Remove leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        // Find the key (between quotes)
        size_t keyStart = line.find('"');
        if (keyStart == std::string::npos) continue;
        size_t keyEnd = line.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) continue;
        
        std::string key = line.substr(keyStart + 1, keyEnd - keyStart - 1);
        
        // Find the value (after colon)
        size_t colonPos = line.find(':', keyEnd);
        if (colonPos == std::string::npos) continue;
        
        // Extract value (skip comma if present)
        size_t valueStart = colonPos + 1;
        size_t valueEnd = line.find_first_of(",}", valueStart);
        if (valueEnd == std::string::npos) valueEnd = line.length();
        
        std::string valueStr = line.substr(valueStart, valueEnd - valueStart);
        // Remove whitespace
        valueStr.erase(std::remove_if(valueStr.begin(), valueStr.end(), ::isspace), valueStr.end());
        
        try {
            double value = std::stod(valueStr);
            
            // Parse the key "body1;body2" and compute hashes
            size_t semicolonPos = key.find(';');
            if (semicolonPos != std::string::npos) {
                std::string body1 = key.substr(0, semicolonPos);
                std::string body2 = key.substr(semicolonPos + 1);
                
                // Trim whitespace from body strings
                body1.erase(std::remove_if(body1.begin(), body1.end(), ::isspace), body1.end());
                body2.erase(std::remove_if(body2.begin(), body2.end(), ::isspace), body2.end());
                
                // Compute hashes
                size_t hash1 = hasher(body1);
                size_t hash2 = hasher(body2);
                
                // Store with sorted order
                std::pair<size_t, size_t> hashPair;
                if (hash1 <= hash2) {
                    hashPair = std::make_pair(hash1, hash2);
                } else {
                    hashPair = std::make_pair(hash2, hash1);
                }
                
                bodyHashPair2Jaccard[hashPair] = value;
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not parse value for key: " << key << std::endl;
        }
    }
    
    file.close();
    if (verbose) {
        std::cout << "Loaded " << bodyHashPair2Jaccard.size() << " body hash pair Jaccard similarities from " << filepath << std::endl;
    }
}

const std::unordered_map<std::pair<size_t, size_t>, double, Loader::PairHash>& Loader::getBodyHashPair2Jaccard() const {
    return bodyHashPair2Jaccard;
}


