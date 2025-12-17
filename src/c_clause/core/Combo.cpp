#include "Combo.h"
#include "Globals.h"
#include <iostream>

Combo::Combo(std::vector<Rule*>& rules, int numTrue, int numPreds) {
    this->memberRules = rules;
    this->length = rules.size();
    this->numTrue = numTrue;
    this->numPreds = numPreds;
    this->confidence = (numPreds > 0) ? (double)numTrue / (double)numPreds : 0.0;
    
    // Sort by rule ID
    std::sort(memberRules.begin(), memberRules.end(), 
              [](Rule* a, Rule* b) { return a->getID() < b->getID(); });
    
    computeHash();
    
    if (comboDebug) {
        std::cout << "[Combo] Created combo with " << length << " rules, conf=" << confidence << std::endl;
    }
}

void Combo::computeHash() {
    hashCode = 0;
    for (Rule* r : memberRules) {
        hashCode = hashCode * 100000007LL + r->getID();
    }
}
