// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "platform.h"
#include <interoplib.h>

#include "comwrappers.h"

using OBJECTHANDLE = InteropLib::OBJECTHANDLE;
using RuntimeCallContext = InteropLibImports::RuntimeCallContext;

namespace InteropLib
{
    void Shutdown() noexcept
    {
        TrackerObjectManager::OnShutdown();
    }

    // Exposed COM related API
    namespace Com
    {
        HRESULT CreateWrapperForObject(
            _In_ OBJECTHANDLE instance,
            _In_ INT32 vtableCount,
            _In_ void* vtablesRaw,
            _In_ INT32 flagsRaw,
            _Outptr_ IUnknown** wrapper) noexcept
        {
            _ASSERTE(instance != nullptr && wrapper != nullptr);

            // Validate the supplied vtable data is valid with a
            // reasonable count.
            if ((vtablesRaw == nullptr && vtableCount != 0) || vtableCount < 0)
                return E_INVALIDARG;

            HRESULT hr;

            // Convert inputs to appropriate types.
            auto flags = static_cast<CreateComInterfaceFlags>(flagsRaw);
            auto vtables = static_cast<ComInterfaceEntry*>(vtablesRaw);

            ManagedObjectWrapper* mow;
            RETURN_IF_FAILED(ManagedObjectWrapper::Create(flags, instance, vtableCount, vtables, &mow));

            *wrapper = static_cast<IUnknown*>(mow->As(IID_IUnknown));
            return S_OK;
        }

        void DestroyWrapperForObject(_In_ void* wrapperMaybe) noexcept
        {
            ManagedObjectWrapper* wrapper = ManagedObjectWrapper::MapFromIUnknown(static_cast<IUnknown*>(wrapperMaybe));

            // A caller should not be destroying a wrapper without knowing if the wrapper is valid.
            _ASSERTE(wrapper != nullptr);

            ManagedObjectWrapper::Destroy(wrapper);
        }

        HRESULT CreateWrapperForExternal(
            _In_ IUnknown* external,
            _In_ INT32 flagsRaw,
            _In_ size_t contextSize,
            _Outptr_ void** context,
            _Outptr_ IUnknown** agileReference) noexcept
        {
            _ASSERTE(external != nullptr && context != nullptr && agileReference != nullptr);

            HRESULT hr;

            // Convert input to appropriate type.
            auto flags = static_cast<CreateObjectFlags>(flagsRaw);

            NativeObjectWrapperContext* wrapperContext;
            RETURN_IF_FAILED(NativeObjectWrapperContext::Create(external, flags, contextSize, &wrapperContext));

            ComHolder<IAgileReference> reference;
            RETURN_IF_FAILED(CreateAgileReference(external, &reference));
            *agileReference = reference.Detach();

            *context = wrapperContext->GetRuntimeContext();
            return S_OK;
        }

        void DestroyWrapperForExternal(_In_ void* contextMaybe) noexcept
        {
            NativeObjectWrapperContext* context = NativeObjectWrapperContext::MapFromRuntimeContext(contextMaybe);

            // A caller should not be destroying a context without knowing if the context is valid.
            _ASSERTE(context != nullptr);

            // Check if the tracker object manager should be informed prior to being destroyed.
            if (context->GetReferenceTracker() != nullptr)
            {
                // We only call this during a GC so ignore the failure as
                // there is no way we can handle that failure at this point.
                HRESULT hr = TrackerObjectManager::BeforeWrapperDestroyed(context);
                _ASSERTE(SUCCEEDED(hr));
                (void)hr;
            }

            NativeObjectWrapperContext::Destroy(context);
        }

        bool IsReferenceTrackerInstance(_In_ void* contextMaybe) noexcept
        {
            NativeObjectWrapperContext* context = NativeObjectWrapperContext::MapFromRuntimeContext(contextMaybe);
            if (context == nullptr)
                return false;

            return (context->GetReferenceTracker() != nullptr);
        }

        bool RegisterReferenceTrackerHostRuntimeImpl(_In_ OBJECTHANDLE objectHandle) noexcept
        {
            return TrackerObjectManager::TrySetReferenceTrackerHostRuntimeImpl(objectHandle);
        }

        void GetIUnknownImpl(
            _Out_ void** fpQueryInterface,
            _Out_ void** fpAddRef,
            _Out_ void** fpRelease) noexcept
        {
            ManagedObjectWrapper::GetIUnknownImpl(fpQueryInterface, fpAddRef, fpRelease);
        }

        HRESULT EnsureActiveWrapperAndAddRef(_In_ IUnknown* wrapperMaybe, _In_opt_ OBJECTHANDLE handle) noexcept
        {
            ManagedObjectWrapper* wrapper = ManagedObjectWrapper::MapFromIUnknown(wrapperMaybe);
            if (wrapper == nullptr)
                return E_INVALIDARG;

            ULONG count = wrapper->AddRef();
            if (count == 1)
            {
                // No object was supplied so reactivation isn't possible.
                if (handle == nullptr)
                {
                    wrapper->Release();
                    return E_HANDLE;
                }

                ::InterlockedExchangePointer(&wrapper->Target, handle);
                return S_FALSE;
            }

            return S_OK;
        }

        HRESULT BeginExternalObjectReferenceTracking(_In_ RuntimeCallContext* cxt) noexcept
        {
            return TrackerObjectManager::BeginReferenceTracking(cxt);
        }

        HRESULT EndExternalObjectReferenceTracking() noexcept
        {
            return TrackerObjectManager::EndReferenceTracking();
        }
    }
}