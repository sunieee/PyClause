#include "Handler.h"

#include <functional>
#include <array>
#include <chrono>
#include <string>


//*** BackendHandler ***

BackendHandler::BackendHandler(){}


void BackendHandler::setRankingOptions(std::map<std::string, std::string> options, ApplicationHandler& ranker){
    

    // register options for ranker

     struct OptionHandler {
        std::string name;
        std::function<void(std::string)> setter;
    };

    std::vector<OptionHandler> handlers = {
        {"topk", [&ranker](std::string val) { ranker.setTopK(std::stoi(val)); }},
        {"disc_at_least", [&ranker](std::string val) { ranker.setDiscAtLeast(std::stoi(val)); }},
        {"hard_stop_at", [&ranker](std::string val) { ranker.setNumPreselect(std::stoi(val)); }},
        {"num_top_rules", [&ranker](std::string val) {ranker.setScoreNumTopRules(std::stoi(val));}},
        {"filter_w_data", [&ranker](std::string val) { ranker.setFilterWTrain(util::stringToBool(val)); }},
        {"filter_w_target", [&ranker](std::string val) { ranker.setFilterWtarget(util::stringToBool(val)); }},
        {"tie_handling", [&ranker](std::string val) { ranker.setTieHandling(val); }},
        {"num_threads", [&ranker](std::string val) { ranker.setNumThr(std::stoi(val)); }},
        {"adapt_topk", [&ranker](std::string val) { ranker.setAdaptTopK(util::stringToBool(val)); }},

    };
    
    // NOTE: ComboHandler configuration is now loaded from Loader, not from options
    // This ensures all handlers (RankingHandler, QAHandler, PredictionHandler) share the same configuration

    //maxplus vs num_top_rules
    auto numTopRules = options.find("num_top_rules");
    if (numTopRules != options.end()) {
        std::string aggFunc = ranker.getComboHandler().getAggregationFunction();
        if (aggFunc == "maxplus" && numTopRules->second != "-1") {
            std::cerr <<
             "Warning: Aggregation function is set to 'maxplus' and 'num_top_rules' is not -1. "
             "Please only do this when you know what you are doing. Otherwise set num_top_rules to -1. "
             "Current value is " 
             << numTopRules->second << std::endl;
        }
    }

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