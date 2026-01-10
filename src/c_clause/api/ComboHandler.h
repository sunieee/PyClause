#ifndef COMBOHANDLER_H
#define COMBOHANDLER_H

#include <string>
#include <map>
#include <iostream>
#include <functional>
#include <vector>
#include <algorithm>

/**
 * ComboHandler manages combo rule aggregation settings.
 * These settings are shared by both RankingHandler and PredictionHandler
 * when using noisyor aggregation with combo rules.
 */
class ComboHandler {
public:
    ComboHandler() {}
    
    // Setters for combo aggregation parameters
    void setIfLoad(bool load) {
        if_load = load;
    }
    
    void setIfDebug(bool debug) {
        if_debug = debug;
    }
    
    void setNoisyorPositiveMethod(std::string method) {
        noisyor_positive_method = method;
    }
    
    void setNoisyorNegativeMethod(std::string method) {
        noisyor_negative_method = method;
    }
    
    void setLiftRatio(double ratio) {
        lift_ratio = ratio;
    }
    
    void setQueryTopK(int num) {
        query_topk = num;
    }
    
    void setAggregationFunction(std::string func) {
        aggregation_function = func;
    }
    
    // Getters
    bool getIfLoad() const { return if_load; }
    bool getIfDebug() const { return if_debug; }
    std::string getNoisyorPositiveMethod() const { return noisyor_positive_method; }
    std::string getNoisyorNegativeMethod() const { return noisyor_negative_method; }
    double getLiftRatio() const { return lift_ratio; }
    int getQueryTopK() const { return query_topk; }
    std::string getAggregationFunction() const { return aggregation_function; }
    
    // Load options from configuration map - for pre-extracted options (already without prefix)
    void loadOptions(const std::map<std::string, std::string>& options, bool verbose = false) {
        // Debug output to verify loadOptions is called
        std::cout << ">>> ComboHandler::loadOptions called with " << options.size() << " options" << std::endl;
        for (const auto& opt : options) {
            std::cout << ">>>   Option: " << opt.first << " = " << opt.second << std::endl;
        }
        
        // Helper to convert string to bool
        auto strToBool = [](const std::string& str) -> bool {
            std::string lower = str;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return (lower == "true" || lower == "1" || lower == "yes");
        };
        
        struct OptionHandler {
            std::string name;
            std::function<void(std::string)> setter;
        };

        std::vector<OptionHandler> handlers = {
            {"if_load", [this, strToBool](std::string val) { setIfLoad(strToBool(val)); }},
            {"if_debug", [this, strToBool](std::string val) { setIfDebug(strToBool(val)); }},
            {"noisyor_positive_method", [this](std::string val) { setNoisyorPositiveMethod(val); }},
            {"noisyor_negative_method", [this](std::string val) { setNoisyorNegativeMethod(val); }},
            {"lift_ratio", [this](std::string val) { setLiftRatio(std::stod(val)); }},
            {"query_topk", [this](std::string val) { setQueryTopK(std::stoi(val)); }},
            {"aggregation_function", [this](std::string val) { setAggregationFunction(val); }},
        };

        for (auto& handler : handlers) {
            auto opt = options.find(handler.name);
            if (opt != options.end()) {
                if (verbose) {
                    std::cout << "Setting combo_handler option " << handler.name 
                              << " to: " << opt->second << std::endl;
                }
                handler.setter(opt->second);
            }
        }
    }
    
    // Load options directly from full options map (extracts combo_handler.* or unprefixed options)
    // This is the main entry point - handlers should call this instead of loadOptions
    void loadOptionsFromMap(const std::map<std::string, std::string>& fullOptions, bool verbose = false) {
        if (verbose) {
            std::cout << ">>> ComboHandler::loadOptionsFromMap called with " << fullOptions.size() << " total options" << std::endl;
        }
        
        // List of combo_handler option names (without prefix)
        std::vector<std::string> comboOptionNames = {
            "noisyor_positive_method", "noisyor_negative_method", 
            "lift_ratio", "query_topk", "aggregation_function", 
            "if_load", "if_debug"
        };
        
        // Check if any combo options exist
        bool hasComboOptions = false;
        for (const auto& optName : comboOptionNames) {
            if (fullOptions.count("combo_handler." + optName) || fullOptions.count(optName)) {
                hasComboOptions = true;
                break;
            }
        }
        
        if (!hasComboOptions) {
            if (verbose) {
                std::cout << ">>> No combo_handler options found" << std::endl;
            }
            return;
        }
        
        if (verbose) {
            std::cout << ">>> Found combo_handler options, extracting them..." << std::endl;
        }
        
        // Extract combo options (try prefixed first, then unprefixed)
        std::map<std::string, std::string> comboOptions;
        for (const auto& optName : comboOptionNames) {
            // Try prefixed version first
            std::string prefixedKey = "combo_handler." + optName;
            auto prefixedOpt = fullOptions.find(prefixedKey);
            if (prefixedOpt != fullOptions.end()) {
                comboOptions[optName] = prefixedOpt->second;
                if (verbose) {
                    std::cout << ">>>   Extracted (prefixed): " << optName << " = " << prefixedOpt->second << std::endl;
                }
                continue;
            }
            
            // Try unprefixed version
            auto unprefixedOpt = fullOptions.find(optName);
            if (unprefixedOpt != fullOptions.end()) {
                comboOptions[optName] = unprefixedOpt->second;
                if (verbose) {
                    std::cout << ">>>   Extracted (unprefixed): " << optName << " = " << unprefixedOpt->second << std::endl;
                }
            }
        }
        
        // Load the extracted options
        if (!comboOptions.empty()) {
            loadOptions(comboOptions, verbose);
        }
    }

private:
    // Whether to load combo rules from rule files
    bool if_load = true;
    
    // Enable debug output for combo rule loading and processing
    bool if_debug = false;
    
    // Positive method: how to aggregate synergistic (positive lift) combo edges
    // Options: "none", "mst", "matching1", "matching2"
    std::string noisyor_positive_method = "none";
    
    // Negative method: how to handle conflicting (negative lift) combo edges
    // Options: "none", "prune", "cluster"
    std::string noisyor_negative_method = "none";
    
    // Scaling factor (alpha) for positive synergy lift values
    double lift_ratio = 1.0;
    
    // Number of top queries/triples to output debug information for (thread 0 only)
    int query_topk = 100;
    
    // Aggregation function: "maxplus" or "noisyor"
    // maxplus: max-aggregation (highest rule confidence)
    // noisyor: noisy-or aggregation (combines multiple rule confidences)
    std::string aggregation_function = "maxplus";
};

#endif // COMBOHANDLER_H
