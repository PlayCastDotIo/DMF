// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
// Compiles the actual upstream method; only framework boundaries are injected.
#include <cstdint>
#include <cstdio>
#include <atomic>
using VOID = void;
using LONG = std::int32_t;
using NTSTATUS = std::int32_t;
using WDFREQUEST = void*;
using ULONG_PTR = std::uintptr_t;
constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_INVALID_DEVICE_STATE = static_cast<NTSTATUS>(0xc0000184u);
constexpr NTSTATUS STATUS_CANCELLED = static_cast<NTSTATUS>(0xc0000120u);
constexpr NTSTATUS STATUS_INSUFFICIENT_RESOURCES = static_cast<NTSTATUS>(0xc000009au);
#define NT_SUCCESS(status) ((status) >= 0)
#define _IRQL_requires_max_(level)
#define FuncEntry(...)
#define FuncExit(...)
#define TraceEvents(...)
#define DMFMODULE_VALIDATE_IN_METHOD(...)
#define WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(...)
struct WDF_OBJECT_ATTRIBUTES {};
struct REQUESTCONTEXT { std::int64_t Timestamp{}; };
struct DMF_CONTEXT_NotifyUserWithRequest {
    LONG EventCountHeld{};
    void* EventRequestQueue{};
    void* DmfModuleTime{};
};
struct DMF_CONFIG_NotifyUserWithRequest { LONG MaximumNumberOfPendingRequests = 1; bool TimeStamping{}; };
struct Module {
    DMF_CONTEXT_NotifyUserWithRequest context;
    DMF_CONFIG_NotifyUserWithRequest config;
    NTSTATUS reference = STATUS_SUCCESS, allocation = STATUS_SUCCESS, forward = STATUS_SUCCESS;
    REQUESTCONTEXT requestContext;
} module;
using DMFMODULE = Module*;
#define DMF_CONTEXT_GET(handle) (&(handle)->context)
#define DMF_CONFIG_GET(handle) (&(handle)->config)
static LONG InterlockedIncrement(LONG* count) { return ++std::atomic_ref<LONG>(*count); }
static LONG InterlockedDecrement(LONG* count) { return --std::atomic_ref<LONG>(*count); }
static NTSTATUS DMF_ModuleReference(DMFMODULE handle) { return handle->reference; }
static void DMF_ModuleDereference(DMFMODULE) {}
static NTSTATUS WdfObjectAllocateContext(WDFREQUEST, WDF_OBJECT_ATTRIBUTES*, VOID** result) {
    *result = &module.requestContext; return module.allocation;
}
static std::int64_t DMF_Time_TickCountGet(void*) { return 42; }
static NTSTATUS WdfRequestForwardToIoQueue(WDFREQUEST, void*) { return module.forward; }
#include "NotifyRequestAdd.inc"

static unsigned failures;
static void Expect(NTSTATUS actual, NTSTATUS expected, const char* scenario) {
    if (actual != expected) { ++failures; std::fprintf(stderr, "%s: %08x != %08x\n", scenario,
        static_cast<unsigned>(actual), static_cast<unsigned>(expected)); }
}
static NTSTATUS Add() { return DMF_NotifyUserWithRequest_EventRequestAdd(&module, &module); }
static void FailedForwardReleasesCapacity(NTSTATUS failure) {
    module = {}; module.forward = failure; Expect(Add(), failure, "failed forward result");
    module.forward = STATUS_SUCCESS; Expect(Add(), STATUS_SUCCESS, "next explicit enqueue after failed forward");
}
static void FailedTimestampAllocationReleasesCapacity() {
    module = {}; module.config.TimeStamping = true; module.allocation = STATUS_INSUFFICIENT_RESOURCES;
    Expect(Add(), STATUS_INSUFFICIENT_RESOURCES, "timestamp allocation failure");
    module.allocation = STATUS_SUCCESS; Expect(Add(), STATUS_SUCCESS, "next enqueue after allocation failure");
}
static void CapacityRejectionDoesNotReleaseAnExistingRequest() {
    module = {}; Expect(Add(), STATUS_SUCCESS, "first accepted request");
    Expect(Add(), STATUS_INVALID_DEVICE_STATE, "second request rejected at limit");
    Expect(Add(), STATUS_INVALID_DEVICE_STATE, "third request remains rejected");
    // Inject the framework cancellation/return boundary for the accepted request.
    InterlockedDecrement(&module.context.EventCountHeld);
    Expect(Add(), STATUS_SUCCESS, "capacity reusable after queue cancellation");
}
static void FailedModuleReferenceDoesNotReserveCapacity() {
    module = {}; module.reference = STATUS_INVALID_DEVICE_STATE;
    Expect(Add(), STATUS_INVALID_DEVICE_STATE, "module reference failure");
    module.reference = STATUS_SUCCESS; Expect(Add(), STATUS_SUCCESS, "next enqueue after reference failure");
}

int main() {
    FailedForwardReleasesCapacity(STATUS_CANCELLED);
    FailedForwardReleasesCapacity(STATUS_INVALID_DEVICE_STATE);
    FailedTimestampAllocationReleasesCapacity();
    CapacityRejectionDoesNotReleaseAnExistingRequest();
    FailedModuleReferenceDoesNotReserveCapacity();
    std::printf("NotifyUserWithRequest: 5 contract scenarios, %u failures\n", failures);

    return failures ? 1 : 0;
}