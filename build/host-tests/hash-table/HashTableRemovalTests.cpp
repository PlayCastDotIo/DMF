// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
#include "HashTableTestHost.h"
#include "Dmf_HashTable.h"
#include "HashTable.inc"
#include <algorithm>
#include <array>
#include <map>
#include <cstdio>
#include <thread>
#include <atomic>
namespace {
std::atomic<unsigned> failures{};
unsigned scenarios;
void Expect(bool condition, const char* scenario) {
    if (!condition) { ++failures; std::fprintf(stderr, "HashTable: %s\n", scenario); }
}
class Table {
    DMF_CONFIG_HashTable config{};
    DMFMODULE module{};
    static ULONG_PTR Collision(DMFMODULE, UCHAR*, ULONG) { return 0; }
public:
    explicit Table(bool collision = false) {
        config.MaximumKeyLength = 4; config.MaximumValueLength = 4; config.MaximumTableSize = 4;
        config.EvtHashTableHashCalculate = collision ? Collision : nullptr;
        DMF_MODULE_ATTRIBUTES attributes{&config}; WDF_OBJECT_ATTRIBUTES objects{};
        Expect(DMF_HashTable_Create(nullptr, &attributes, &objects, &module) == STATUS_SUCCESS, "create");
    }
    ~Table() { HostModuleDelete(module); }
    NTSTATUS Write(ULONG key, ULONG value) {
        return DMF_HashTable_Write(module, reinterpret_cast<UCHAR*>(&key), sizeof(key),
            reinterpret_cast<UCHAR*>(&value), sizeof(value));
    }
    NTSTATUS Read(ULONG key, ULONG* value) {
        ULONG bytes = 0;
        return DMF_HashTable_Read(module, reinterpret_cast<UCHAR*>(&key), sizeof(key),
            reinterpret_cast<UCHAR*>(value), sizeof(*value), &bytes);
    }
    NTSTATUS Remove(ULONG key) { return DMF_HashTable_Remove(module, reinterpret_cast<UCHAR*>(&key), sizeof(key)); }
    NTSTATUS RemoveExpected(ULONG key, ULONG expected) {
        return DMF_HashTable_RemoveEx(module, reinterpret_cast<UCHAR*>(&key), sizeof(key),
            [](DMFMODULE, VOID* context, UCHAR*, ULONG, UCHAR* bytes, ULONG length) -> BOOLEAN {
                ULONG current = 0; if (length != sizeof(current)) return FALSE;
                std::memcpy(&current, bytes, sizeof(current)); return current == *static_cast<ULONG*>(context);
            }, &expected);
    }
    NTSTATUS IncrementExisting(ULONG key) {
        return DMF_HashTable_FindExistingEx(module, reinterpret_cast<UCHAR*>(&key), sizeof(key),
            [](DMFMODULE, VOID*, UCHAR*, ULONG, UCHAR* bytes, ULONG* length) {
                Expect(*length == sizeof(ULONG), "lookup preserves value length");
                ULONG current = 0; std::memcpy(&current, bytes, sizeof(current)); ++current;
                std::memcpy(bytes, &current, sizeof(current));
            }, this);
    }
    NTSTATUS RemoveWithInjectedChainFault(ULONG key, ULONG head, std::array<ULONG, 4> next) {
        // Simulate damaged framework-owned storage after the public lookup but
        // before removal. Do not call the private traversal helper directly.
        auto* context = DMF_CONTEXT_GET(module);
        const auto savedHead = context->HashMap[0];
        std::array<ULONG, 4> savedNext{};
        for (ULONG i = 0; i < 4; ++i)
            savedNext[i] = HashTable_IndexToDataEntry(context, i)->NextEntryIndex;
        struct Fault { ULONG head; std::array<ULONG, 4> next; } fault{head, next};
        const auto status = DMF_HashTable_RemoveEx(module, reinterpret_cast<UCHAR*>(&key), sizeof(key),
            [](DMFMODULE handle, VOID* injected, UCHAR*, ULONG, UCHAR*, ULONG) -> BOOLEAN {
                auto* storage = DMF_CONTEXT_GET(handle);
                const auto& damage = *static_cast<Fault*>(injected);
                storage->HashMap[0] = damage.head;
                for (ULONG i = 0; i < 4; ++i)
                    HashTable_IndexToDataEntry(storage, i)->NextEntryIndex = damage.next[i];
                return TRUE;
            }, &fault);
        // Restore only the injected links; public reads/removal below verify
        // that the rejected operation did not modify values or reclaim entries.
        context->HashMap[0] = savedHead;
        for (ULONG i = 0; i < 4; ++i)
            HashTable_IndexToDataEntry(context, i)->NextEntryIndex = savedNext[i];
        return status;
    }
    std::map<ULONG, ULONG> Enumerate() {
        std::map<ULONG, ULONG> values;
        DMF_HashTable_Enumerate(module,
            [](DMFMODULE, UCHAR* keyBytes, ULONG keyLength, UCHAR* valueBytes, ULONG valueLength, VOID* context) -> BOOLEAN {
                ULONG key = 0, value = 0;
                Expect(keyLength == sizeof(key) && valueLength == sizeof(value), "enumeration lengths");
                std::memcpy(&key, keyBytes, sizeof(key)); std::memcpy(&value, valueBytes, sizeof(value));
                Expect(static_cast<std::map<ULONG, ULONG>*>(context)->emplace(key, value).second, "no duplicate key");
                return TRUE;
            }, &values);
        return values;
    }
};
void EveryRemovalOrder(bool collision) {
    std::array<ULONG, 4> order{1, 2, 3, 4};
    do {
        ++scenarios; Table table(collision); std::map<ULONG, ULONG> remaining;
        for (ULONG key = 1; key <= 4; ++key) { Expect(table.Write(key, key + 10) == STATUS_SUCCESS, "initial write"); remaining[key] = key + 10; }
        for (auto key : order) {
            Expect(table.Remove(key) == STATUS_SUCCESS, "remove head/middle/tail/compacted entry"); remaining.erase(key);
            Expect(table.Enumerate() == remaining, "collision chain and dense enumeration survive removal");
            ULONG value = 0; Expect(table.Read(key, &value) == STATUS_NOT_FOUND, "removed key absent");
            for (auto [survivor, expected] : remaining)
                Expect(table.Read(survivor, &value) == STATUS_SUCCESS && value == expected, "survivor readable");
        }
        Expect(table.Write(99, 8) == STATUS_SUCCESS, "fully emptied capacity reusable");
    } while (std::next_permutation(order.begin(), order.end()));
}
void MissingLookupsDoNotReserveEntries() {
    ++scenarios; Table table;
    for (ULONG key = 100; key < 200; ++key) {
        Expect(table.IncrementExisting(key) == STATUS_NOT_FOUND, "missing lookup does not insert");
        Expect(table.Remove(key) == STATUS_NOT_FOUND, "missing removal does not insert");
    }
    for (ULONG key = 1; key <= 4; ++key) Expect(table.Write(key, key) == STATUS_SUCCESS, "capacity intact after misses");
}
void ConditionalRemovalPreservesReplacement() {
    ++scenarios; Table table;
    Expect(table.Write(1, 8) == STATUS_SUCCESS, "write expected value");
    Expect(table.RemoveExpected(1, 7) == STATUS_NOT_FOUND, "predicate mismatch");
    Expect(table.RemoveExpected(1, 8) == STATUS_SUCCESS, "predicate match");
    Expect(table.Write(1, 9) == STATUS_SUCCESS, "replacement write");
    Expect(table.RemoveExpected(1, 8) == STATUS_NOT_FOUND, "stale cleanup cannot delete replacement");
    ULONG value = 0; Expect(table.Read(1, &value) == STATUS_SUCCESS && value == 9, "replacement preserved");
}
void ReclaimedSlotsSupportDistinctKeys() {
    ++scenarios; Table table(true);
    for (ULONG key = 1; key <= 1000; ++key) {
        Expect(table.Write(key, key) == STATUS_SUCCESS, "distinct key insertion after removal");
        Expect(table.Remove(key) == STATUS_SUCCESS, "capacity reclaimed repeatedly");
    }
}
void ExistingCallbacksRemainSerialized() {
    ++scenarios; Table table; Expect(table.Write(1, 0) == STATUS_SUCCESS, "concurrent fixture write");
    std::array<std::thread, 4> actors;
    for (auto& actor : actors) actor = std::thread([&] {
        for (unsigned i = 0; i < 1000; ++i) table.IncrementExisting(1);
    });
    for (auto& actor : actors) actor.join();
    ULONG value = 0; Expect(table.Read(1, &value) == STATUS_SUCCESS && value == 4000, "locked callback updates serialized");
}
void ModelAgreesAcrossMixedRemovalsAndReplacementWrites() {
    ++scenarios; Table table(true); std::map<ULONG, ULONG> model; ULONG state = 42;
    for (unsigned step = 0; step < 5000; ++step) {
        state = state * 1664525u + 1013904223u; ULONG key = (state >> 16) % 8;
        if (state & 1) {
            auto expected = model.contains(key) || model.size() < 4 ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
            Expect(table.Write(key, state) == expected, "model write capacity");
            if (expected == STATUS_SUCCESS) model[key] = state;
        } else {
            Expect(table.Remove(key) == (model.contains(key) ? STATUS_SUCCESS : STATUS_NOT_FOUND), "model removal");
            model.erase(key);
        }
        Expect(table.Enumerate() == model, "model enumeration after compaction");
        ULONG actual = 0;
        for (auto [present, value] : model)
            Expect(table.Read(present, &actual) == STATUS_SUCCESS && actual == value, "model read after compaction");
    }
}
void CorruptRemovalChainsFailWithoutChangingEntries() {
    const std::array<std::pair<ULONG, std::array<ULONG, 4>>, 5> faults{{
        {1, {1, 1, 3, INVALID_INDEX}}, // Self-cycle hides the selected entry.
        {1, {1, 2, 1, INVALID_INDEX}}, // Multiple-node cycle hides it.
        {4, {1, 2, 3, INVALID_INDEX}}, // Bucket head is outside allocated entries.
        {1, {1, 4, 3, INVALID_INDEX}}, // A next link is outside allocated entries.
        {0, {1, 2, 1, INVALID_INDEX}}, // Selected entry exists; compaction target is hidden.
    }};
    for (const auto& [head, next] : faults) {
        ++scenarios; Table table(true); std::map<ULONG, ULONG> expected;
        for (ULONG key = 1; key <= 4; ++key) {
            Expect(table.Write(key, key + 10) == STATUS_SUCCESS, "corruption fixture write");
            expected[key] = key + 10;
        }
        Expect(table.RemoveWithInjectedChainFault(1, head, next) == STATUS_INVALID_DEVICE_STATE,
            "cyclic or out-of-range removal chain rejects without hanging");
        Expect(table.Enumerate() == expected, "failed removal preserves all entries and values");
        Expect(table.Remove(1) == STATUS_SUCCESS, "rejected removal releases the lock");
    }
}
}
int RunHashTableRemovalTests() {
    EveryRemovalOrder(false); EveryRemovalOrder(true);
    MissingLookupsDoNotReserveEntries(); ConditionalRemovalPreservesReplacement();
    ReclaimedSlotsSupportDistinctKeys(); ExistingCallbacksRemainSerialized();
    ModelAgreesAcrossMixedRemovalsAndReplacementWrites();
    CorruptRemovalChainsFailWithoutChangingEntries();
    std::printf("HashTable: %u contract scenarios, %u failures\n", scenarios, failures.load());
    return failures ? 1 : 0;
}

int main() { return RunHashTableRemovalTests(); }
