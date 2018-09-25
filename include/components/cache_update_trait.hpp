#pragma once

#include <chrono>
#include <string>

#include <utils/periodic_task.hpp>

#include "cache_config.hpp"

namespace components {

class CacheUpdateTrait {
 public:
  enum class UpdateType { kFull, kIncremental };

  void UpdateFull();

 protected:
  CacheUpdateTrait(CacheConfig&& config, const std::string& name);
  virtual ~CacheUpdateTrait();

  void StartPeriodicUpdates();
  void StopPeriodicUpdates();

 private:
  virtual void Update(UpdateType type,
                      const std::chrono::system_clock::time_point& last_update,
                      const std::chrono::system_clock::time_point& now) = 0;

  void DoPeriodicUpdate();

  CacheConfig config_;
  const std::string name_;
  utils::PeriodicTask update_task_;

  std::chrono::system_clock::time_point last_update_;
  std::chrono::steady_clock::time_point last_full_update_;
};

}  // namespace components
