/* XMRigCC
 * Copyright 2017-     BenDr0id    <https://github.com/BenDr0id>, <ben@graef.in>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __CLIENT_STATUS_H__
#define __CLIENT_STATUS_H__

#include <string>
#include <ctime>
#include <list>
#include <rapidjson/document.h>
#include "GPUInfo.h"

class ClientStatus
{

public:
  enum Status
  {
    RUNNING,
    PAUSED
  };

public:
  ClientStatus();

  inline const char* toString(Status status)
  {
    return status_str[static_cast<int>(status)];
  }

  inline Status toStatus(const char* status)
  {
    const int n = sizeof(status_str) / sizeof(status_str[0]);
    for (int i = 0; i < n; ++i)
    {
      if (strcmp(status_str[i], status) == 0)
      {
        return (Status) i;
      }
    }
    return Status::RUNNING;
  }

  Status getCurrentStatus() const;
  void setCurrentStatus(Status currentStatus);

  std::string getClientId() const;
  void setClientId(const std::string& clientId);

  std::string getCurrentPool() const;
  void setCurrentPool(const std::string& currentPool);

  std::string getCurrentPoolUser() const;
  void setCurrentPoolUser(const std::string& currentPoolUser);

  std::string getCurrentPoolPass() const;
  void setCurrentPoolPass(const std::string& currentPoolPass);

  std::string getCurrentPoolRigId() const;
  void setCurrentPoolRigId(const std::string& currentPoolRigId);

  std::string getCurrentAlgoName() const;
  void setCurrentAlgoName(const std::string& algoName);

  std::string getCurrentPowVariantName() const;
  void setCurrentPowVariantName(const std::string& powVariantName);

  std::string getCpuBrand() const;
  void setCpuBrand(const std::string& cpuBrand);

  std::string getExternalIp() const;
  void setExternalIp(const std::string& externalIp);

  std::string getVersion() const;
  void setVersion(const std::string& version);

  std::string getLog() const;
  void setLog(const std::string& log);

  void clearLog();

  std::string getAssembly() const;
  void setAssembly(const std::string& assembly);

  bool hasHugepages() const;
  void setHugepages(bool hasHugepages);

  bool isHugepagesEnabled() const;
  void setHugepagesEnabled(bool hugepagesEnabled);

  bool isCpuX64() const;
  void setCpuX64(bool isCpuX64);

  bool isVM() const;
  void setVM(bool isVM);

  bool hasCpuAES() const;
  void setCpuAES(bool hasCpuAES);

  double getHashrateShort() const;
  void setHashrateShort(double hashrateShort);

  double getHashrateMedium() const;
  void setHashrateMedium(double hashrateMedium);

  double getHashrateLong() const;
  void setHashrateLong(double hashrateLong);

  void setHashrateHighest(double hashrateHighest);
  double getHashrateHighest() const;

  int getHashFactor() const;
  void setHashFactor(int hashFactor);

  int getTotalPages() const;
  void setTotalPages(int totalPages);

  int getTotalHugepages() const;
  void setTotalHugepages(int totalHugepages);

  int getCurrentThreads() const;
  void setCurrentThreads(int currentThreads);

  int getCurrentWays() const;
  void setCurrentWays(int currentWays);

  int getCpuSockets() const;
  void setCpuSockets(int cpuSockets);

  int getCpuCores() const;
  void setCpuCores(int cpuCores);

  int getCpuThreads() const;
  void setCpuThreads(int cpuThreads);

  int getMaxCpuUsage() const;
  void setMaxCpuUsage(int maxCpuUsage);

  int getCpuL2() const;
  void setCpuL2(int cpuL2);

  int getCpuL3() const;
  void setCpuL3(int cpuL3);

  uint64_t getTotalMemory() const;
  void setTotalMemory(uint64_t totalMemory);

  uint64_t getFreeMemory() const;
  void setFreeMemory(uint64_t freeMemory);

  void setNodes(int nodes);
  int getNodes();

  const std::list<GPUInfo> getGPUInfoList() const;

  void addGPUInfo(const GPUInfo& gpuInfo);

  void clearGPUInfoList();

  uint64_t getSharesGood() const;
  void setSharesGood(uint64_t sharesGood);

  uint64_t getSharesTotal() const;
  void setSharesTotal(uint64_t sharesTotal);

  uint64_t getHashesTotal() const;
  void setHashesTotal(uint64_t hashesTotal);

  void setAvgTime(uint32_t avgTime);
  uint32_t getAvgTime() const;

  uint64_t getLastStatusUpdate() const;

  void setUptime(uint64_t uptime);
  uint64_t getUptime() const;

  std::string toJsonString();

  rapidjson::Value toJson(rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>& allocator);

  bool parseFromJson(const rapidjson::Document& document);

private:
  const char* status_str[3] = {
    "RUNNING",
    "PAUSED",
    "CONFIG_UPDATED"
  };

  Status m_currentStatus = Status::PAUSED;

  std::string m_clientId;
  std::string m_currentPool;
  std::string m_currentPoolUser;
  std::string m_currentPoolPass;
  std::string m_currentPoolRigId;
  std::string m_currentAlgoName;
  std::string m_currentPowVariantName;
  std::string m_cpuBrand;
  std::string m_externalIp;
  std::string m_version;
  std::string m_log;
  std::string m_assembly;

  bool m_hasHugepages = false;
  bool m_isHugepagesEnabled = false;
  bool m_isCpuX64 = false;
  bool m_hasCpuAES = false;
  bool m_isVM = false;

  double m_hashrateShort = 0;
  double m_hashrateMedium = 0;
  double m_hashrateLong = 0;
  double m_hashrateHighest = 0;

  int m_hashFactor = 0;
  int m_totalPages = 0;
  int m_totalHugepages = 0;
  int m_currentThreads = 0;
  int m_currentWays = 0;
  int m_cpuSockets = 0;
  int m_cpuCores = 0;
  int m_cpuThreads = 0;
  int m_cpuL2 = 0;
  int m_cpuL3 = 0;
  int m_nodes = 0;
  int m_maxCpuUsage = 0;

  std::list<GPUInfo> m_gpuInfoList;

  uint64_t m_sharesGood = 0;
  uint64_t m_sharesTotal = 0;
  uint64_t m_hashesTotal = 0;
  uint64_t m_uptime = 0;
  uint64_t m_totalMemory = 0;
  uint64_t m_freeMemory = 0;

  uint32_t m_avgTime = 0;
  uint64_t m_lastStatusUpdate = 0;
};

#endif /* __CLIENT_STATUS_H__ */
