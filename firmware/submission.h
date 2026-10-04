#pragma once
#include <string>
namespace snowball {
inline bool journalAdmitted(const std::string& status) {
  return status=="queued" || status=="dispatched" || status=="acknowledged" || status=="completed";
}
inline bool ownJournalAdmission(const std::string& expected,const std::string& receipt,const std::string& status,bool connected){
  return connected&&!expected.empty()&&receipt=="cmd_m5_"+expected&&journalAdmitted(status);
}
}
