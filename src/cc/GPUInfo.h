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

#ifndef XMRIG_GPUINFO_H
#define XMRIG_GPUINFO_H

#include <string>
#include "3rdparty/rapidjson/document.h"

class GPUInfo
{
public:
  GPUInfo();

  ~GPUInfo();

  rapidjson::Value toJson(rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>& allocator);

  bool parseFromJson(const rapidjson::Value& gpuInfo);

  uint32_t getDeviceIdx() const;
  void setDeviceIdx(uint32_t deviceIdx);

  uint32_t getIntensity() const;
  void setIntensity(uint32_t intensity);

  uint32_t getWorkSize() const;
  void setWorkSize(uint32_t workSize);

  size_t getMemory() const;
  void setMemory(size_t memory);

  size_t getFreeMem() const;
  void setFreeMem(size_t freeMem);

  uint32_t getThreads() const;
  void setThreads(uint32_t threads);

  uint32_t getBlocks() const;
  void setBlocks(uint32_t blocks);

  uint32_t getBfactor() const;
  void setBfactor(uint32_t bfactor);

  uint32_t getBsleep() const;
  void setBsleep(uint32_t bsleep);

  uint32_t getComputeUnits() const;
  void setComputeUnits(uint32_t computeUnits);

  uint32_t getClock() const;
  void setClock(uint32_t clock);

  std::string getName() const;
  void setName(const std::string& name);

  std::string getType() const;
  void setType(const std::string& type);

  std::string getBusId() const;
  void setBusId(const std::string& busId);

private:
  size_t m_memory{0};
  size_t m_freeMem{0};

  uint32_t m_deviceIdx{0};
  uint32_t m_intensity{0};
  uint32_t m_workSize{0};
  uint32_t m_threads{0};
  uint32_t m_computeUnits{0};
  uint32_t m_blocks{0};
  uint32_t m_bfactor{0};
  uint32_t m_bsleep{0};
  uint32_t m_clock{0};

  std::string m_name;
  std::string m_type;
  std::string m_busId;
};


#endif //XMRIG_GPUINFO_H
