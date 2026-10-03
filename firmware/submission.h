#pragma once
#include <string>
namespace snowball {
inline bool journalAdmitted(const std::string& status) {
  return status=="queued" || status=="dispatched" || status=="acknowledged" || status=="completed";
}
}
