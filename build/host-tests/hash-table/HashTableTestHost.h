// Copyright (c) 2026 Playcast, Inc. Licensed under the MIT license.
// Injected framework only. Tests call the production module's public methods.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <mutex>
#include <memory>
#include <sal.h>
using VOID = void;
using UCHAR = unsigned char;
using ULONG = std::uint32_t;
using ULONG_PTR = std::uintptr_t;
using BOOLEAN = unsigned char;
using NTSTATUS = std::int32_t;
using WDFDEVICE = void*;
constexpr BOOLEAN FALSE = 0, TRUE = 1;
constexpr NTSTATUS STATUS_SUCCESS = 0;
constexpr NTSTATUS STATUS_NOT_FOUND = static_cast<NTSTATUS>(0xc0000225u);
constexpr NTSTATUS STATUS_BUFFER_TOO_SMALL = static_cast<NTSTATUS>(0xc0000023u);
constexpr NTSTATUS STATUS_INVALID_PARAMETER = static_cast<NTSTATUS>(0xc000000du);
constexpr NTSTATUS STATUS_INVALID_DEVICE_STATE = static_cast<NTSTATUS>(0xc0000184u);
constexpr NTSTATUS STATUS_BUFFER_OVERFLOW = static_cast<NTSTATUS>(0x80000005u);
#define ANYSIZE_ARRAY 1
#define FIELD_OFFSET(type, member) static_cast<ULONG>(offsetof(type, member))
#define MAX_NATURAL_ALIGNMENT 8
#define NT_SUCCESS(status) ((status) >= 0)
#define _IRQL_requires_max_(level)
#define _IRQL_requires_same_
#define FuncEntry(...)
#define FuncExit(...)
#define FuncExitVoid(...)
#define TraceEvents(...)
#define DmfAssert(condition) ((void)(condition))
#define PAGED_CODE()
#define UNREFERENCED_PARAMETER(value) ((void)(value))
struct Memory { std::vector<UCHAR> bytes; };
using WDFMEMORY = Memory*;
struct HostModule;
using DMFMODULE = HostModule*;
struct WDF_OBJECT_ATTRIBUTES { void* ParentObject{}; };
struct DMF_MODULE_ATTRIBUTES { void* ModuleConfigPointer{}; };
struct DMF_CALLBACKS_DMF {
    NTSTATUS (*DeviceOpen)(DMFMODULE){};
    VOID (*DeviceClose)(DMFMODULE){};
};
struct DMF_MODULE_DESCRIPTOR { std::size_t ContextSize{}; DMF_CALLBACKS_DMF* CallbacksDmf{}; };
struct HostModule {
    std::vector<UCHAR> context;
    void* config{};
    VOID (*close)(DMFMODULE){};
    std::mutex lock;
};
#define DECLARE_DMF_MODULE(name) NTSTATUS DMF_##name##_Create(WDFDEVICE, DMF_MODULE_ATTRIBUTES*, WDF_OBJECT_ATTRIBUTES*, DMFMODULE*);
#define DMF_MODULE_DECLARE_CONTEXT(name)
#define DMF_MODULE_DECLARE_CONFIG(name)
#define DMF_CONTEXT_GET(handle) reinterpret_cast<DMF_CONTEXT_HashTable*>((handle)->context.data())
#define DMF_CONFIG_GET(handle) static_cast<DMF_CONFIG_HashTable*>((handle)->config)
#define DMFMODULE_VALIDATE_IN_METHOD(...)
#define WDF_OBJECT_ATTRIBUTES_INIT(attributes) (*(attributes) = {})
#define DMF_CALLBACKS_DMF_INIT(callbacks) (*(callbacks) = {})
#define DMF_MODULE_DESCRIPTOR_INIT_CONTEXT_TYPE(descriptor, name, context, options, open) (descriptor).ContextSize = sizeof(context)
constexpr int NonPagedPoolNx = 0;
static VOID DMF_ModuleLock(DMFMODULE module) { module->lock.lock(); }
static VOID DMF_ModuleUnlock(DMFMODULE module) { module->lock.unlock(); }
static BOOLEAN DMF_ModuleIsLocked(DMFMODULE) { return TRUE; }
static VOID WdfObjectDelete(WDFMEMORY memory) { delete memory; }
static NTSTATUS WdfMemoryCreate(WDF_OBJECT_ATTRIBUTES*, int, ULONG, std::size_t size,
    WDFMEMORY* result, VOID** buffer) {
    auto memory = std::make_unique<Memory>(); memory->bytes.resize(size);
    *buffer = memory->bytes.data(); *result = memory.release(); return STATUS_SUCCESS;
}
static VOID RtlCopyMemory(VOID* to, const VOID* from, std::size_t bytes) {
    if (bytes) std::memcpy(to, from, bytes);
}
static VOID RtlZeroMemory(VOID* buffer, std::size_t bytes) { std::memset(buffer, 0, bytes); }
static VOID RtlSecureZeroMemory(VOID* buffer, std::size_t bytes) { RtlZeroMemory(buffer, bytes); }
static VOID RtlFillMemory(VOID* buffer, std::size_t bytes, ULONG value) {
    std::memset(buffer, static_cast<unsigned char>(value), bytes);
}
static std::size_t RtlCompareMemory(const VOID* left, const VOID* right, std::size_t bytes) {
    auto a = static_cast<const UCHAR*>(left); auto b = static_cast<const UCHAR*>(right);
    std::size_t equal = 0; while (equal < bytes && a[equal] == b[equal]) ++equal; return equal;
}
static NTSTATUS DMF_ModuleCreate(WDFDEVICE, DMF_MODULE_ATTRIBUTES* attributes,
    WDF_OBJECT_ATTRIBUTES*, DMF_MODULE_DESCRIPTOR* descriptor, DMFMODULE* result) {
    auto module = std::make_unique<HostModule>(); module->context.resize(descriptor->ContextSize);
    module->config = attributes->ModuleConfigPointer; module->close = descriptor->CallbacksDmf->DeviceClose;
    auto status = descriptor->CallbacksDmf->DeviceOpen(module.get());
    if (status != STATUS_SUCCESS) return status;
    *result = module.release(); return STATUS_SUCCESS;
}
static VOID HostModuleDelete(DMFMODULE module) { module->close(module); delete module; }
