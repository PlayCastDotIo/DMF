// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
// Production cancellation and terminal method; injected WDF/module boundaries.
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <condition_variable>
#include <future>
#include <chrono>
#include <atomic>
using VOID = void;
using LONG = std::int32_t;
using NTSTATUS = std::int32_t;
using ULONG_PTR = std::uintptr_t;
constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_CANCELLED = static_cast<NTSTATUS>(0xc0000120u);
constexpr NTSTATUS STATUS_DELETE_PENDING = static_cast<NTSTATUS>(0xc0000056u);
constexpr NTSTATUS STATUS_INVALID_DEVICE_STATE = static_cast<NTSTATUS>(0xc0000184u);
constexpr NTSTATUS STATUS_DEVICE_BUSY = static_cast<NTSTATUS>(0x80000011u);
#define NT_SUCCESS(Status) ((Status) >= 0)
#define _IRQL_requires_max_(Level)
#define PAGED_CODE() ((void)0)
#define DMFMODULE_VALIDATE_IN_METHOD(...)
#define FuncEntry(...)
#define FuncExitVoid(...)
#define TraceEvents(...)
#define DmfAssert(Condition) ((void)0)
namespace {
struct Module;
struct Queue;
struct Request { Queue* queue; };
using WDFQUEUE = Queue*;
using WDFREQUEST = Request*;
using DMFMODULE = Module*;
using Cancel = void (*)(DMFMODULE, WDFREQUEST, ULONG_PTR, NTSTATUS);
struct DMF_CONTEXT_NotifyUserWithRequest { WDFQUEUE EventRequestQueue{}; LONG EventCountHeld{}; };
struct DMF_CONFIG_NotifyUserWithRequest { Cancel EvtPendingRequestsCancel{}; };
struct Module {
    DMF_CONTEXT_NotifyUserWithRequest context;
    DMF_CONFIG_NotifyUserWithRequest config;
    NTSTATUS referenceStatus = STATUS_SUCCESS;
    unsigned references{}, releases{};
};
struct Queue {
    DMFMODULE module;
    std::mutex gate;
    std::condition_variable changed;
    bool pending{}, completed{}, callbackEntered{}, callbackReturned{}, purged{}, defer{}, holdReturn{}, releaseReturn{};
    unsigned purges{}, completions{}, captures{};
};
#define DMF_CONTEXT_GET(Module) (&(Module)->context)
#define DMF_CONFIG_GET(Module) (&(Module)->config)
static LONG InterlockedDecrement(LONG* Count) { return --std::atomic_ref<LONG>(*Count); }
static DMFMODULE* WdfObjectGet_DMFMODULE(WDFQUEUE Queue) { return &Queue->module; }
static NTSTATUS DMF_ModuleReference(DMFMODULE Module) {
    if (NT_SUCCESS(Module->referenceStatus)) ++Module->references;
    return Module->referenceStatus;
}
static void DMF_ModuleDereference(DMFMODULE Module) { ++Module->releases; }
static void WdfRequestComplete(WDFREQUEST Request, NTSTATUS Status) {
    auto* queue = Request->queue;
    std::lock_guard held(queue->gate);
    if (Status != STATUS_CANCELLED) std::fprintf(stderr, "Unexpected cancellation status\n");
    ++queue->completions; queue->completed = true; queue->changed.notify_all();
}
#include "NotifyRequestCancel.inc"
static void WdfIoQueuePurgeSynchronously(WDFQUEUE Queue) {
    std::unique_lock held(Queue->gate);
    Queue->purged = true; ++Queue->purges;
    if (Queue->pending) {
        Queue->pending = false; Request request{Queue};
        held.unlock(); EvtIoCanceledOnQueue(Queue, &request); held.lock();
        Queue->callbackReturned = true; Queue->changed.notify_all();
        Queue->changed.wait(held, [&] { return Queue->completed; });
    }
}
#include "Dmf_NotifyUserWithRequest.CancelAndJoin.h"
unsigned scenarios{}, failures{};
static void Expect(bool Condition, const char* Label) {
    if (!Condition) { ++failures; std::fprintf(stderr, "Notify cancel/join: %s\n", Label); }
}
static void Capture(DMFMODULE Module, WDFREQUEST Request, ULONG_PTR Context, NTSTATUS Status) {
    auto* queue = Request->queue;
    std::unique_lock held(queue->gate);
    ++queue->captures; queue->callbackEntered = true; queue->changed.notify_all();
    if (Module != queue->module || Context != 0 || Status != STATUS_CANCELLED)
        std::fprintf(stderr, "Unexpected capture metadata\n");
    if (!queue->defer) {
        held.unlock(); WdfRequestComplete(Request, Status); held.lock();
    }
    if (queue->holdReturn) queue->changed.wait(held, [&] { return queue->releaseReturn; });
}
struct Fixture {
    Module module;
    Queue queue{&module};
    Fixture() { module.context.EventRequestQueue = &queue; }
    void Pending(bool Configured) {
        queue.pending = true; module.context.EventCountHeld = 1;
        if (Configured) module.config.EvtPendingRequestsCancel = Capture;
    }
    void Balanced() { Expect(module.references == module.releases, "module retained through join and released"); }
};
static void RejectedReference() {
    ++scenarios; Fixture f; f.module.referenceStatus = STATUS_DELETE_PENDING;
    Expect(DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(&f.module) == STATUS_DELETE_PENDING, "reference failure preserved");
    Expect(f.queue.purges == 0, "failed reference does not touch queue"); f.Balanced();
}
static void MissingQueue() {
    ++scenarios; Fixture f; f.module.context.EventRequestQueue = nullptr;
    Expect(DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(&f.module) == STATUS_INVALID_DEVICE_STATE, "missing queue rejects join");
    Expect(f.queue.purges == 0, "missing queue is not purged"); f.Balanced();
}
static void EmptyTerminalQueue() {
    ++scenarios; Fixture f;
    Expect(DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(&f.module) == STATUS_SUCCESS, "empty terminal join");
    Expect(DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(&f.module) == STATUS_SUCCESS, "serialized repeated terminal join");
    Expect(f.queue.purged && f.queue.purges == 2, "queue remains purged"); f.Balanced();
}
static void PendingCancellation(bool Configured) {
    ++scenarios; Fixture f; f.Pending(Configured);
    Expect(DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(&f.module) == STATUS_SUCCESS, "pending request joined");
    Expect(f.queue.completions == 1 && f.queue.captures == unsigned(Configured), "configured capture or default completion exactly once");
    Expect(f.queue.callbackReturned && f.module.context.EventCountHeld == 0, "cancellation accounting retired"); f.Balanced();
}
static void WaitForDeferredCompletionOrCallbackReturn(bool HoldReturn) {
    ++scenarios; Fixture f; f.Pending(true); f.queue.defer = !HoldReturn; f.queue.holdReturn = HoldReturn;
    auto joined = std::async(std::launch::async, [&] { return DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(&f.module); });
    {
        std::unique_lock held(f.queue.gate);
        f.queue.changed.wait(held, [&] { return HoldReturn ? f.queue.completed : f.queue.callbackReturned; });
    }
    Expect(joined.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout,
        HoldReturn ? "native completion does not bypass callback return" : "callback return does not bypass deferred native completion");
    {
        std::lock_guard held(f.queue.gate);
        if (HoldReturn) f.queue.releaseReturn = true;
        else { f.queue.completed = true; ++f.queue.completions; }
        f.queue.changed.notify_all();
    }
    Expect(joined.get() == STATUS_SUCCESS, "join after both barriers");
    Expect(f.queue.completions == 1 && f.queue.captures == 1, "no retry or direct duplicate completion"); f.Balanced();
}
static void ResidualAccountingRejectsRetirement() {
    ++scenarios; Fixture f; f.module.context.EventCountHeld = 1;
    Expect(DMF_NotifyUserWithRequest_EventRequestCancelAndJoin(&f.module) == STATUS_DEVICE_BUSY, "residual accounting rejects success");
    Expect(f.queue.purged && f.queue.completions == 0, "fault retains terminal queue without invented completion"); f.Balanced();
}
}
int RunNotifyRequestCancelAndJoinTests() {
    RejectedReference(); MissingQueue(); EmptyTerminalQueue(); PendingCancellation(false); PendingCancellation(true);
    WaitForDeferredCompletionOrCallbackReturn(false); WaitForDeferredCompletionOrCallbackReturn(true);
    ResidualAccountingRejectsRetirement();
    std::printf("Notify cancel/join: %u contract scenarios, %u failures\n", scenarios, failures);
    return failures ? 1 : 0;
}

int main() { return RunNotifyRequestCancelAndJoinTests(); }
