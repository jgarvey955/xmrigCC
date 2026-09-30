/* XMRig
 * Copyright (c) 2018-2019 tevador     <tevador@gmail.com>
 * Copyright (c) 2018-2020 SChernykh   <https://github.com/SChernykh>
 * Copyright (c) 2016-2020 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
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

#ifndef XMRIG_RX_VM_H
#define XMRIG_RX_VM_H


#include <cstdint>


#include "base/crypto/Algorithm.h"


class randomx_vm;


namespace xmrig
{


class Assembly;
class RxDataset;


class RxVm
{
public:
    // Number of attempts create() makes before it gives up and returns nullptr.
    static constexpr uint32_t kCreateAttempts = 3;

    // Returns nullptr if the VM could not be created after kCreateAttempts attempts.
    static randomx_vm *create(RxDataset *dataset, uint8_t *scratchpad, bool softAes, const Assembly &assembly, uint32_t node);
    static void destroy(randomx_vm *vm);

    // Zecnero NU2: why `algorithm` is not hashed with a VM made by create() on
    // `dataset`, with what to do about it, or nullptr if it is. Only an
    // algorithm with RxAlgo::isFastJitOnly() can be unsupported.
    static const char *unsupported(Algorithm::Id algorithm, const RxDataset *dataset);
    static const char *unsupported(Algorithm::Id algorithm, bool fullMem, bool jit, bool lightByConfig);

    // Zecnero NU2: false if `algorithm` must not be hashed on `dataset`
    // (unsupported() is not nullptr). Logs why, with what to do, once each time
    // the miner switches to that algorithm. Mining rx/zecnero on a dataset that
    // could not mine rx/zecnero2 logs a warning once, ahead of NU2.
    static bool isSupported(const Algorithm &algorithm, const RxDataset *dataset, uint64_t height);

#   ifdef XMRIG_RX_TEST_HOOKS
    // Test hook: the next `count` calls to randomx_create_vm made by create() report failure.
    static void failNextCreates(uint32_t count);
#   endif
};


} /* namespace xmrig */


#endif /* XMRIG_RX_VM_H */
