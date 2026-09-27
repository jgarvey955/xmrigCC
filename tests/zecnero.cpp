/* Zecnero tests. SPDX-License-Identifier: GPL-3.0-or-later */
#include "3rdparty/rapidjson/document.h"
#include "base/tools/Cvt.h"
#include "base/tools/zecnero/BlockTemplate.h"
#include "base/net/stratum/Job.h"
#include "base/net/stratum/Pool.h"
#include "base/kernel/config/BaseTransform.h"
#include "base/kernel/interfaces/IConfig.h"
#include "crypto/common/VirtualMemory.h"
#include "crypto/randomx/randomx.h"
#include "crypto/rx/RxAlgo.h"
#include "crypto/rx/RxCache.h"
#include "crypto/rx/RxDataset.h"
#include "backend/cpu/Cpu.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <iterator>
#include <algorithm>

using namespace xmrig;
static void check(bool ok, const char *what) { if (!ok) { throw std::runtime_error(what); } }

static rapidjson::Document sample()
{
    rapidjson::Document d;
    d.Parse(R"({"version":4,"height":2113,"seedheight":2048,
      "seedhash":"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
      "previousblockhash":"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
      "defaultroots":{"merkleroot":"202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"},
      "blockcommitmentshash":"404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f",
      "mintime":16909059,"curtime":16909060,"maxtime":16909061,
      "bits":"207fffff","target":"7fffff0000000000000000000000000000000000000000000000000000000000",
      "sizelimit":2000000,"noncerange":"00000000ffffffff",
      "coinbasetxn":{"data":"010203"},"transactions":[{"data":"04050607"}]})");
    check(!d.HasParseError(), "fixture JSON");
    return d;
}

static void protocol()
{
    check(Algorithm("rx/zecnero") == Algorithm::RX_ZECNERO, "algorithm name registration");
    check(Algorithm("rx/zecnero2") == Algorithm::RX_ZECNERO2 && Algorithm("rx2/zecnero") == Algorithm::RX_ZECNERO2, "v2 aliases");
    std::string error;
    ZecneroBlockTemplate b;
    auto d = sample();
    check(b.parse(d, error), error.c_str());
    check(b.header[0] == 4 && b.header[4] == 31 && b.header[35] == 0, "previous hash byte order");
    check(b.header[36] == 63 && b.header[67] == 32, "merkle byte order");
    check(b.header[68] == 95 && b.header[99] == 64, "commitment byte order");
    check(b.header[100] == 4 && b.header[103] == 1 && b.header[107] == 0x20, "time and bits endian");
    check(b.seed[0] == 0 && b.seed[31] == 31, "seed must not be reversed");
    std::array<uint8_t,28> extra; extra.fill(0xab); b.setExtraNonce(extra.data());
    auto block = b.block(0x12345678);
    check(block.size() == 149 && block[108] == 0x78 && block[111] == 0x12 && block[112] == 0xab && block[139] == 0xab, "nonce serialization");
    check(block[140] == 0 && block[141] == 2 && block[142] == 1 && block.back() == 7, "empty solution and intact transactions");
    Job job(false, Algorithm::RX_ZECNERO, String());
    check(job.setBlob(Cvt::toHex(b.header)) && job.nonceOffset() == 108 && job.size() == 140 && !job.isNicehash(), "Zecnero job layout");
    check(job.setSeedHash(Cvt::toHex(b.seed)) && job.setFullTarget(Cvt::toHex(b.target)), "Zecnero job target/seed");
    std::array<uint8_t,32> hash;
    std::reverse_copy(b.target.begin(), b.target.end(), hash.begin());
    check(job.meetsTarget(hash.data()), "inclusive target equality");
    hash[0] = 1; check(!job.meetsTarget(hash.data()), "reject lower-limb overflow with identical upper 64 bits");
    hash[31]--; check(job.meetsTarget(hash.data()), "below target");
    Job copy(job); Job moved(std::move(copy));
    hash[31]++; check(!moved.meetsTarget(hash.data()), "copy/move preserve target");
    Job different(job); auto seed = b.seed; seed[0]++;
    different.setSeedHash(Cvt::toHex(seed)); check(different != job, "seed change updates work");
    check(!job.setFullTarget(std::string(64, '0').c_str()), "reject zero target");
    check(!job.setBlob("01020304"), "reject wrong header length");

    for (const auto height : {1u, 2112u, 2113u, 4160u, 4161u}) {
        auto x = sample(); x["height"].SetUint(height); x["seedheight"].SetUint(height <= 2112 ? 0 : ((height-65)/2048)*2048);
        check(b.parse(x, error), "seed boundary");
        x["seedheight"].SetUint(x["seedheight"].GetUint()+1); check(!b.parse(x, error), "wrong seed height");
    }
    for (const char *name : {"version","height","seedhash","previousblockhash","defaultroots","blockcommitmentshash","curtime","mintime","maxtime","target","bits","sizelimit","coinbasetxn","transactions"}) {
        auto x=sample(); x.RemoveMember(name); check(!b.parse(x,error), name);
    }
    { auto x=sample(); x["target"].SetString(std::string(64,'f').c_str(), x.GetAllocator()); check(!b.parse(x,error), "bits target mismatch"); }
    { auto x=sample(); x["curtime"].SetUint(0); check(!b.parse(x,error), "out-of-range time"); }
    { auto x=sample(); x["transactions"][0]["data"].SetString("g0"); check(!b.parse(x,error), "bad tx hex"); }
    { auto x=sample(); x.AddMember("powversion",2,x.GetAllocator()); x.AddMember("algo","rx/zecnero2",x.GetAllocator());
      check(b.parse(x,error) && b.powVersion==2, "v2 template");
      Job v2(false, Algorithm::RX_ZECNERO2, String());
      check(v2.setBlob(Cvt::toHex(b.header)) && v2.nonceOffset()==108 && v2.setFullTarget(Cvt::toHex(b.target)), "v2 nonce and target");
      x["algo"].SetString("rx/zecnero"); check(!b.parse(x,error), "version/algorithm mismatch"); }
    { auto x=sample(); x.AddMember("powversion",3,x.GetAllocator()); check(!b.parse(x,error), "reject unknown PoW version"); }
    { auto x=sample(); x["sizelimit"].SetUint(142); check(!b.parse(x,error), "oversized block"); }
    { auto x=sample(); x["transactions"].Clear(); for(unsigned i=0;i<252;i++) { rapidjson::Value tx(rapidjson::kObjectType); tx.AddMember("data","ff",x.GetAllocator()); x["transactions"].PushBack(tx,x.GetAllocator()); }
      check(b.parse(x,error), "253 transactions"); const auto raw=b.block(0); check(raw[141]==253 && raw[142]==253 && raw[143]==0, "CompactSize 253"); }
    { rapidjson::Document p; p.Parse(R"({"url":"127.0.0.1:18732","algo":"rx/zecnero","daemon":true,"user":"u","pass":"p","daemon-cookie-file":"/tmp/cookie","daemon-cookie-source":"https://localhost:18734/mining/cookie","daemon-cookie-auth":"miner1:test-only","daemon-cookie-fingerprint":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","daemon-rpc-user":"rpc"})");
      Pool pool(p); rapidjson::Document out; auto encoded=pool.toJSON(out); Pool decoded(encoded);
      check(pool==decoded && decoded.daemonCookieFile()=="/tmp/cookie" && decoded.daemonCookieSource()=="https://localhost:18734/mining/cookie" && decoded.daemonCookieAuth()=="miner1:test-only" && decoded.daemonCookieFingerprint()=="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" && decoded.daemonRpcUser()=="rpc" && decoded.password()=="p", "RPC auth config roundtrip"); }
    { BaseTransform base; IConfigTransform &transform = base; rapidjson::Document config(rapidjson::kObjectType);
      transform.transform(config, IConfig::UrlKey, "127.0.0.1:18732");
      transform.transform(config, IConfig::DaemonCookieFileKey, "./.cookie");
      transform.transform(config, IConfig::DaemonCookieSourceKey, "https://localhost:18734");
      transform.transform(config, IConfig::DaemonCookieAuthKey, "miner:test-only");
      transform.transform(config, IConfig::DaemonCookieFingerprintKey, std::string(64, 'a').c_str());
      transform.transform(config, IConfig::DaemonRpcUserKey, "rpc-user");
      transform.transform(config, IConfig::UserKey, "payout-wallet");
      Pool pool(config["pools"][0]);
      check(pool.daemonCookieFile()=="./.cookie" && pool.daemonCookieSource()=="https://localhost:18734" &&
            pool.daemonCookieAuth()=="miner:test-only" && pool.daemonCookieFingerprint()==std::string(64,'a').c_str() &&
            pool.daemonRpcUser()=="rpc-user" && pool.user()=="payout-wallet", "Zecnero CLI fields and wallet stay separate"); }
    std::cout << "Protocol, target boundaries, seed epochs and config tests passed\n";
}

static void vectors(const char *path)
{
    std::ifstream file(path); check(bool(file), "open vector file");
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    rapidjson::Document d; d.Parse(text.c_str()); check(!d.HasParseError(), "vector JSON");
    VirtualMemory::init(0, 2048);
    RxAlgo::apply(d["randomx"]["program_version"].GetUint() == 2 ? Algorithm::RX_ZECNERO2 : Algorithm::RX_ZECNERO);
    VirtualMemory scratch(2*1024*1024, false, false, false);
    RxCache cache(false, 0); check(cache.get()!=nullptr, "cache allocation");
    for (const auto flags : {RANDOMX_FLAG_DEFAULT, RANDOMX_FLAG_JIT}) {
        auto vm=randomx_create_vm(flags, cache.get(), nullptr, scratch.raw(), 0); check(vm!=nullptr, "VM allocation");
        for (const auto &v : d["cases"].GetArray()) {
            auto key=Cvt::fromHex(v["key"].GetString(),v["key"].GetStringLength());
            auto input=Cvt::fromHex(v["input"].GetString(),v["input"].GetStringLength());
            cache.init(key); randomx_vm_set_cache(vm,cache.get());
            uint8_t hash[32]; randomx_calculate_hash(vm,input.data(),input.size(),hash);
            if (Cvt::toHex(hash,32)!=v["hash"].GetString()) { throw std::runtime_error(std::string("vector mismatch: ")+v["name"].GetString()); }
        }
        randomx_destroy_vm(vm);
        std::cout << d["cases"].Size() << " Zecnero vectors passed (" << (flags==RANDOMX_FLAG_JIT ? "JIT" : "interpreted") << ")\n";
    }
}
static void fastVectors(const char *v1, const char *v2)
{
    VirtualMemory::init(0, 2048);
    RxAlgo::apply(Algorithm::RX_ZECNERO);
    RxDataset dataset(false, false, true, RxConfig::FastMode, 0);
    check(dataset.get() != nullptr, "fast dataset allocation");
    const auto key = Cvt::fromHex("74657374206b657920303030", 24);
    check(dataset.init(key, 4, -1), "fast dataset initialization");
    VirtualMemory scratch(2*1024*1024, false, false, false);
    for (const char *path : {v1, v2}) {
        std::ifstream file(path);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        rapidjson::Document d; d.Parse(text.c_str()); check(!d.HasParseError(), "fast vector JSON");
        RxAlgo::apply(d["randomx"]["program_version"].GetUint() == 2 ? Algorithm::RX_ZECNERO2 : Algorithm::RX_ZECNERO);
        for (int mode : {0, int(RANDOMX_FLAG_JIT), int(RANDOMX_FLAG_JIT | RANDOMX_FLAG_AMD)}) {
            int flags = mode | RANDOMX_FLAG_FULL_MEM;
            if (Cpu::info()->hasAES()) { flags |= RANDOMX_FLAG_HARD_AES; }
            auto vm = randomx_create_vm(static_cast<randomx_flags>(flags), nullptr, dataset.get(), scratch.raw(), 0);
            check(vm != nullptr, "fast VM creation");
            for (unsigned i = 0; i < 3; ++i) {
                const auto &v = d["cases"][i];
                auto input = Cvt::fromHex(v["input"].GetString(), v["input"].GetStringLength());
                uint8_t hash[32]; randomx_calculate_hash(vm, input.data(), input.size(), hash);
                check(Cvt::toHex(hash, 32) == v["hash"].GetString(), "full-memory known answer");
                uint64_t temp[8]; randomx_calculate_hash_first(vm, temp, input.data(), input.size());
                randomx_calculate_hash_next(vm, temp, input.data(), input.size(), hash);
                check(Cvt::toHex(hash, 32) == v["hash"].GetString(), "pipelined full-memory known answer");
            }
            randomx_destroy_vm(vm);
        }
    }
    std::cout << "v1/v2 full-memory and pipelined known answers passed (interpreter, JIT and Ryzen JIT)\n";
}

int main(int argc,char **argv)
{
    try { check(argc==2 || argc==3,"expected one or two vector paths"); protocol(); if(argc==2 && std::string(argv[1])=="--protocol") { return 0; } if(argc==3) { fastVectors(argv[1],argv[2]); } else { vectors(argv[1]); } return 0; }
    catch(const std::exception &e) { std::cerr << "FAILED: " << e.what() << '\n'; return 1; }
}
