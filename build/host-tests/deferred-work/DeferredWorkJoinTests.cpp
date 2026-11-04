// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
// Production method bodies with injected module/WDF/queue boundaries only.
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <condition_variable>
#include <future>
#include <chrono>
using NTSTATUS = std::int32_t;
using LONG = std::int32_t;
constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_DEVICE_BUSY = static_cast<NTSTATUS>(0x80000011u);
constexpr NTSTATUS STATUS_DELETE_PENDING = static_cast<NTSTATUS>(0xc0000056u);
#define NT_SUCCESS(Status) ((Status) >= 0)
#define _IRQL_requires_max_(Level)
#define PAGED_CODE() ((void)0)
#define DMFMODULE_VALIDATE_IN_METHOD(...)
namespace {
struct Module;
using DMFMODULE = Module*;
struct DeferredWork {
    std::mutex gate; std::condition_variable retired;
    bool active{}, flushEntered{};
    unsigned flushes{};
};
struct Context {
    DeferredWork* DeferredOnDemand{};
    LONG NumberOfPendingCalls{};
    DMFMODULE DmfModuleScheduledTask{};
    unsigned* DmfModuleBufferQueue{};
};
using DMF_CONTEXT_ScheduledTask = Context;
using DMF_CONTEXT_QueuedWorkItem = Context;
struct Module { Context context; NTSTATUS status = STATUS_SUCCESS; unsigned references{}, releases{}; };
#define DMF_CONTEXT_GET(Module) (&(Module)->context)
static NTSTATUS DMF_ModuleReference(DMFMODULE module) {
    if (NT_SUCCESS(module->status)) ++module->references;
    return module->status;
}
static void DMF_ModuleDereference(DMFMODULE module) { ++module->releases; }
static void WdfWorkItemFlush(DeferredWork* work) {
    std::unique_lock held(work->gate); ++work->flushes; work->flushEntered = true;
    work->retired.notify_all(); work->retired.wait(held, [&] { return !work->active; });
}
static unsigned DMF_BufferQueue_Count(unsigned* queued) { return *queued; }
#include "Dmf_ScheduledTask.DeferredJoin.h"
#include "Dmf_QueuedWorkItem.Drain.h"
unsigned scenarios{}, failures{};
static void Expect(bool condition, const char* label) {
    if (!condition) { ++failures; std::fprintf(stderr, "Deferred work join: %s\n", label); }
}
struct Fixture {
    DeferredWork work;
    Module scheduled, queued;
    unsigned descriptors{};
    Fixture() {
        scheduled.context.DeferredOnDemand = &work;
        queued.context.DmfModuleScheduledTask = &scheduled;
        queued.context.DmfModuleBufferQueue = &descriptors;
    }
    void Balanced() {
        Expect(scheduled.references == scheduled.releases, "scheduled module references balanced");
        Expect(queued.references == queued.releases, "queue module references balanced");
    }
};
static void SuccessRemainsReusable() {
    ++scenarios; Fixture fixture;
    Expect(DMF_ScheduledTask_DeferredJoin(&fixture.scheduled) == STATUS_SUCCESS, "first join");
    Expect(DMF_ScheduledTask_DeferredJoin(&fixture.scheduled) == STATUS_SUCCESS, "second join");
    Expect(fixture.work.flushes == 2, "join does not stop future on-demand work"); fixture.Balanced();
}
static void ReferenceFailureDoesNotTouchWork(bool queue) {
    ++scenarios; Fixture fixture;
    (queue ? fixture.queued : fixture.scheduled).status = STATUS_DELETE_PENDING;
    Expect((queue ? DMF_QueuedWorkItem_Drain(&fixture.queued) : DMF_ScheduledTask_DeferredJoin(&fixture.scheduled))
        == STATUS_DELETE_PENDING, "reference failure preserved");
    Expect(fixture.work.flushes == 0, "no work access after failed reference"); fixture.Balanced();
}
static void PendingScheduledCallsCannotDeclareRetirement(bool queue) {
    ++scenarios; Fixture fixture; fixture.scheduled.context.NumberOfPendingCalls = 1;
    Expect((queue ? DMF_QueuedWorkItem_Drain(&fixture.queued) : DMF_ScheduledTask_DeferredJoin(&fixture.scheduled))
        == STATUS_DEVICE_BUSY, "pending count rejects retirement"); fixture.Balanced();
}
static void DrainConfirmsAllDescriptorsRetired() {
    ++scenarios; Fixture fixture;
    Expect(DMF_QueuedWorkItem_Drain(&fixture.queued) == STATUS_SUCCESS, "empty queue drain");
    fixture.descriptors = 1;
    Expect(DMF_QueuedWorkItem_Drain(&fixture.queued) == STATUS_DEVICE_BUSY, "orphan descriptor rejects drain");
    fixture.descriptors = 0;
    Expect(DMF_QueuedWorkItem_Drain(&fixture.queued) == STATUS_SUCCESS, "later admitted generation can drain"); fixture.Balanced();
}
static void ChildReferenceFailurePropagates() {
    ++scenarios; Fixture fixture; fixture.scheduled.status = STATUS_DELETE_PENDING;
    Expect(DMF_QueuedWorkItem_Drain(&fixture.queued) == STATUS_DELETE_PENDING, "child join error");
    Expect(fixture.work.flushes == 0, "failed child does not touch work"); fixture.Balanced();
}
static void BlockedReturnAndBufferReuseMustFinish(bool queue) {
    ++scenarios; Fixture fixture;
    fixture.work.active = true; fixture.scheduled.context.NumberOfPendingCalls = 1;
    fixture.descriptors = 1;
    auto joining = std::async(std::launch::async, [&] {
        return queue ? DMF_QueuedWorkItem_Drain(&fixture.queued) : DMF_ScheduledTask_DeferredJoin(&fixture.scheduled);
    });
    { std::unique_lock held(fixture.work.gate);
      fixture.work.retired.wait(held, [&] { return fixture.work.flushEntered; }); }
    Expect(joining.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout, "join waits for native callback return");
    { std::lock_guard held(fixture.work.gate);
      fixture.descriptors = 0; fixture.scheduled.context.NumberOfPendingCalls = 0;
      fixture.work.active = false; fixture.work.retired.notify_all(); }
    Expect(joining.get() == STATUS_SUCCESS, "join succeeds after callback and reuse"); fixture.Balanced();
}
}
int RunDeferredWorkJoinTests() {
    SuccessRemainsReusable(); ReferenceFailureDoesNotTouchWork(false); ReferenceFailureDoesNotTouchWork(true);
    PendingScheduledCallsCannotDeclareRetirement(false); PendingScheduledCallsCannotDeclareRetirement(true);
    DrainConfirmsAllDescriptorsRetired(); ChildReferenceFailurePropagates();
    BlockedReturnAndBufferReuseMustFinish(false); BlockedReturnAndBufferReuseMustFinish(true);
    std::printf("Deferred work join: %u contract scenarios, %u failures\n", scenarios, failures);
    return failures ? 1 : 0;
}

int main() { return RunDeferredWorkJoinTests(); }
