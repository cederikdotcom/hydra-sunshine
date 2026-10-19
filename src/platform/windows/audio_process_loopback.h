/**
 * @file src/platform/windows/audio_process_loopback.h
 * @brief Activate WASAPI capture for one application's process tree.
 */
#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <new>
#include <windows.h>

namespace platf::audio {
  // SDK AUDIOCLIENT_ACTIVATION_PARAMS layout. Private names avoid collisions
  // with SDKs which provide audioclientactivationparams.h.
  struct process_activation_params_t {
    DWORD activation_type = 1;
    DWORD target_pid;
    DWORD loopback_mode = 0;  // INCLUDE_TARGET_PROCESS_TREE
  };

  static_assert(sizeof(process_activation_params_t) == 12);

  class process_activation_handler_t final: public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
  public:
    process_activation_handler_t() {
      CoCreateFreeThreadedMarshaler(static_cast<IActivateAudioInterfaceCompletionHandler *>(this), &marshaler);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
      if (!out) {
        return E_POINTER;
      }
      *out = nullptr;
      if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
        *out = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
      } else if (iid == __uuidof(IAgileObject)) {
        *out = static_cast<IAgileObject *>(this);
      } else if (iid == __uuidof(IMarshal) && marshaler) {
        return marshaler->QueryInterface(iid, out);
      } else {
        return E_NOINTERFACE;
      }
      AddRef();
      return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
      return InterlockedIncrement(&refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
      auto remaining = InterlockedDecrement(&refs);
      if (!remaining) {
        delete this;
      }
      return remaining;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *operation) override {
      IUnknown *unknown = nullptr;
      HRESULT activation_result = E_FAIL;
      result = operation->GetActivateResult(&activation_result, &unknown);
      if (SUCCEEDED(result)) {
        result = activation_result;
      }
      if (SUCCEEDED(result)) {
        result = unknown ? unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void **>(&client)) : E_POINTER;
      }
      if (unknown) {
        unknown->Release();
      }
      SetEvent(done);
      return S_OK;
    }

    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_PENDING;
    IAudioClient *client = nullptr;

  private:
    ~process_activation_handler_t() {
      if (client) {
        client->Release();
      }
      if (marshaler) {
        marshaler->Release();
      }
      if (done) {
        CloseHandle(done);
      }
    }

    LONG refs = 1;
    IUnknown *marshaler = nullptr;
  };

  inline HRESULT activate_process_loopback(DWORD pid, IAudioClient **client) {
    *client = nullptr;
    if (!pid) {
      return E_INVALIDARG;
    }
    // Keep the system module loaded while callbacks may still be outstanding.
    static HMODULE module = LoadLibraryExW(L"mmdevapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using activate_t = HRESULT(WINAPI *)(LPCWSTR, REFIID, PROPVARIANT *, IActivateAudioInterfaceCompletionHandler *, IActivateAudioInterfaceAsyncOperation **);
    static auto activate = module ? reinterpret_cast<activate_t>(GetProcAddress(module, "ActivateAudioInterfaceAsync")) : nullptr;
    if (!activate) {
      return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    auto *handler = new (std::nothrow) process_activation_handler_t;
    if (!handler) {
      return E_OUTOFMEMORY;
    }
    if (!handler->done) {
      handler->Release();
      return E_OUTOFMEMORY;
    }
    process_activation_params_t params {1, pid, 0};
    PROPVARIANT prop {};
    prop.vt = VT_BLOB;
    prop.blob.cbSize = sizeof(params);
    prop.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
    IActivateAudioInterfaceAsyncOperation *operation = nullptr;
    auto result = activate(L"VAD\\Process_Loopback", __uuidof(IAudioClient), &prop, handler, &operation);
    if (SUCCEEDED(result)) {
      auto wait = WaitForSingleObject(handler->done, 5000);
      if (wait == WAIT_OBJECT_0) {
        result = handler->result;
        if (SUCCEEDED(result)) {
          *client = handler->client;
          (*client)->AddRef();
        }
      } else {
        result = HRESULT_FROM_WIN32(wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
      }
    }
    if (operation) {
      operation->Release();
    }
    // Windows retains its own handler reference until the callback completes.
    handler->Release();
    return result;
  }
}  // namespace platf::audio
