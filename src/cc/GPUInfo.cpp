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

#include "GPUInfo.h"

GPUInfo::GPUInfo()
{

}

GPUInfo::~GPUInfo()
{

}

rapidjson::Value GPUInfo::toJson(rapidjson::MemoryPoolAllocator<rapidjson::CrtAllocator>& allocator)
{
  rapidjson::Value gpuInfo(rapidjson::kObjectType);

  gpuInfo.AddMember("name", rapidjson::StringRef(m_name.c_str()), allocator);
  gpuInfo.AddMember("type", rapidjson::StringRef(m_type.c_str()), allocator);
  gpuInfo.AddMember("busId", rapidjson::StringRef(m_busId.c_str()), allocator);

  gpuInfo.AddMember("device_idx", m_deviceIdx, allocator);
  gpuInfo.AddMember("intensity", m_intensity, allocator);
  gpuInfo.AddMember("work_size", m_workSize, allocator);
  gpuInfo.AddMember("threads", m_threads, allocator);
  gpuInfo.AddMember("compute_units", m_computeUnits, allocator);
  gpuInfo.AddMember("block", m_blocks, allocator);
  gpuInfo.AddMember("bfactor", m_bfactor, allocator);
  gpuInfo.AddMember("bsleep", m_bsleep, allocator);
  gpuInfo.AddMember("clock", m_clock, allocator);

  gpuInfo.AddMember("memory", static_cast<uint32_t>(m_memory), allocator);
  gpuInfo.AddMember("free_mem", static_cast<uint32_t>(m_freeMem), allocator);

  return gpuInfo;
}

bool GPUInfo::parseFromJson(const rapidjson::Value& gpuInfo)
{
  bool result = false;

  if (gpuInfo.HasMember("name"))
  {
    m_name = gpuInfo["name"].GetString();
    result = true;
  }

  if (gpuInfo.HasMember("type"))
  {
    m_type = gpuInfo["type"].GetString();
  }

  if (gpuInfo.HasMember("busId"))
  {
    m_busId = gpuInfo["busId"].GetString();
  }

  if (gpuInfo.HasMember("device_idx"))
  {
    m_deviceIdx = gpuInfo["device_idx"].GetInt();
  }

  if (gpuInfo.HasMember("intensity"))
  {
    m_intensity = gpuInfo["intensity"].GetInt();
  }

  if (gpuInfo.HasMember("work_size"))
  {
    m_workSize = gpuInfo["work_size"].GetInt();
  }

  if (gpuInfo.HasMember("threads"))
  {
    m_threads = gpuInfo["threads"].GetInt();
  }

  if (gpuInfo.HasMember("compute_units"))
  {
    m_computeUnits = gpuInfo["compute_units"].GetInt();
  }

  if (gpuInfo.HasMember("block"))
  {
    m_blocks = gpuInfo["block"].GetInt();
  }

  if (gpuInfo.HasMember("bfactor"))
  {
    m_bfactor = gpuInfo["bfactor"].GetInt();
  }

  if (gpuInfo.HasMember("bsleep"))
  {
    m_bsleep = gpuInfo["bsleep"].GetInt();
  }

  if (gpuInfo.HasMember("clock"))
  {
    m_clock = gpuInfo["clock"].GetInt();
  }

  if (gpuInfo.HasMember("free_mem"))
  {
    m_freeMem = static_cast<size_t>(gpuInfo["free_mem"].GetInt());
  }

  if (gpuInfo.HasMember("memory"))
  {
    m_memory = static_cast<size_t>(gpuInfo["memory"].GetInt());
  }

  return result;
}

uint32_t GPUInfo::getDeviceIdx() const
{
  return m_deviceIdx;
}

void GPUInfo::setDeviceIdx(uint32_t deviceIdx)
{
  m_deviceIdx = deviceIdx;
}

uint32_t GPUInfo::getIntensity() const
{
  return m_intensity;
}

void GPUInfo::setIntensity(uint32_t intensity)
{
  m_intensity = intensity;
}

uint32_t GPUInfo::getWorkSize() const
{
  return m_workSize;
}

void GPUInfo::setWorkSize(uint32_t workSize)
{
  m_workSize = workSize;
}

size_t GPUInfo::getMemory() const
{
  return m_memory;
}

void GPUInfo::setMemory(size_t memory)
{
  m_memory = memory;
}

size_t GPUInfo::getFreeMem() const
{
  return m_freeMem;
}

void GPUInfo::setFreeMem(size_t freeMem)
{
  m_freeMem = freeMem;
}

uint32_t GPUInfo::getThreads() const
{
  return m_threads;
}

void GPUInfo::setThreads(uint32_t threads)
{
  m_threads = threads;
}

uint32_t GPUInfo::getBlocks() const
{
  return m_blocks;
}

void GPUInfo::setBlocks(uint32_t blocks)
{
  m_blocks = blocks;
}

uint32_t GPUInfo::getBfactor() const
{
  return m_bfactor;
}

void GPUInfo::setBfactor(uint32_t bfactor)
{
  m_bfactor = bfactor;
}

uint32_t GPUInfo::getBsleep() const
{
  return m_bsleep;
}

void GPUInfo::setBsleep(uint32_t bsleep)
{
  m_bsleep = bsleep;
}

uint32_t GPUInfo::getComputeUnits() const
{
  return m_computeUnits;
}

void GPUInfo::setComputeUnits(uint32_t computeUnits)
{
  m_computeUnits = computeUnits;
}

uint32_t GPUInfo::getClock() const
{
  return m_clock;
}

void GPUInfo::setClock(uint32_t clock)
{
  m_clock = clock;
}

std::string GPUInfo::getName() const
{
  return m_name;
}

void GPUInfo::setName(const std::string& name)
{
  m_name = name;
}

std::string GPUInfo::getType() const
{
  return m_type;
}

void GPUInfo::setType(const std::string& type)
{
  m_type = type;
}

std::string GPUInfo::getBusId() const
{
  return m_busId;
}

void GPUInfo::setBusId(const std::string& busId)
{
  m_busId = busId;
}

