/**
 * @file tools/process_audio_probe.cpp
 * @brief Windows hardware test for the production process-loopback helper.
 *
 * Build with ucrt64 g++ -std=c++20 -O2 -static -Isrc/platform/windows
 * tools/process_audio_probe.cpp -o process_audio_probe.exe
 * -lole32 -loleaut32 -luuid -lwinmm.
 * Run --tone 440 and --tone 880 as separate processes, then capture each PID.
 * Capturing PID 0 selects the probe itself: both tones must be absent.
 */
#define WIN32_LEAN_AND_MEAN
#include "audio_process_loopback.h"

#include <audioclient.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <vector>
#include <windows.h>

int main(int argc, char **argv) {
  if (argc > 2 && strcmp(argv[1], "--tone") == 0) {
    const int rate = 48000;
    const double hz = atof(argv[2]);
    std::vector<short> samples(rate);
    for (int i = 0; i < rate; ++i) {
      samples[i] = (short) (4000 * sin(2 * 3.141592653589793 * hz * i / rate));
    }
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, 1, rate, rate * 2, 2, 16, 0};
    HWAVEOUT output = nullptr;
    if (waveOutOpen(&output, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL)) {
      return 10;
    }
    WAVEHDR header = {};
    header.lpData = (char *) samples.data();
    header.dwBufferLength = samples.size() * 2;
    header.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
    header.dwLoops = 30;
    waveOutPrepareHeader(output, &header, sizeof(header));
    waveOutWrite(output, &header, sizeof(header));
    Sleep(30000);
    waveOutReset(output);
    waveOutUnprepareHeader(output, &header, sizeof(header));
    waveOutClose(output);
    return 0;
  }
  if (argc < 2) {
    printf("usage: probe.exe <pid> [seconds]\n");
    return 2;
  }
  DWORD pid = (DWORD) strtoul(argv[1], nullptr, 10);
  if (!pid) {
    pid = GetCurrentProcessId();
  }
  int seconds = argc > 2 ? atoi(argv[2]) : 20;

  printf("target pid=%lu  seconds=%d\n", (unsigned long) pid, seconds);
  HRESULT ci = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  printf("CoInitializeEx(MTA) hr=0x%08lx\n", (unsigned long) ci);
  APTTYPE at;
  APTTYPEQUALIFIER aq;
  if (SUCCEEDED(CoGetApartmentType(&at, &aq))) {
    printf("apartment type=%d qualifier=%d (1=MTA)\n", (int) at, (int) aq);
  }

  IAudioClient *client = nullptr;
  HRESULT hr = platf::audio::activate_process_loopback(pid, &client);
  printf("activate_process_loopback hr=0x%08lx\n", (unsigned long) hr);
  if (FAILED(hr)) {
    return 1;
  }

  // Process loopback is fussy about format: it requires a hand-built mix
  // format, GetMixFormat is not available on this virtual device.
  WAVEFORMATEXTENSIBLE wf = {};
  wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wf.Format.nChannels = 2;
  wf.Format.nSamplesPerSec = 48000;
  wf.Format.wBitsPerSample = 32;
  wf.Format.nBlockAlign = wf.Format.nChannels * wf.Format.wBitsPerSample / 8;
  wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
  wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  wf.Samples.wValidBitsPerSample = 32;
  wf.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  wf.SubFormat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

  // Exactly Sunshine's flags.
  hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 0, 0, (WAVEFORMATEX *) &wf, nullptr);
  printf("Initialize hr=0x%08lx\n", (unsigned long) hr);
  if (FAILED(hr)) {
    return 1;
  }

  REFERENCE_TIME def = 0, min = 0;
  hr = client->GetDevicePeriod(&def, &min);
  printf("GetDevicePeriod hr=0x%08lx default=%lldms\n", (unsigned long) hr, (long long) (def / 10000));

  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  client->SetEventHandle(ev);
  IAudioCaptureClient *cap = nullptr;
  client->GetService(__uuidof(IAudioCaptureClient), (void **) &cap);
  client->Start();
  printf("capturing...\n");

  int withAudio = 0, silent = 0, empty = 0;
  unsigned long long sample_index = 0;
  double real440 = 0, imag440 = 0, real880 = 0, imag880 = 0, real1320 = 0, imag1320 = 0;
  double peak = 0.0;
  ULONGLONG end = GetTickCount64() + (ULONGLONG) seconds * 1000;
  while (GetTickCount64() < end) {
    if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) {
      empty++;
      continue;
    }
    BYTE *data;
    UINT32 frames;
    DWORD flags;
    while (cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr) == S_OK) {
      if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        silent++;
      } else if (frames) {
        double sum = 0.0;
        float *f = (float *) data;
        for (UINT32 i = 0; i < frames * 2; i++) {
          sum += (double) f[i] * f[i];
        }
        for (UINT32 i = 0; i < frames; ++i, ++sample_index) {
          double value = (f[2 * i] + f[2 * i + 1]) / 2;
          double phase = 2 * 3.141592653589793 * 440 * sample_index / 48000;
          real440 += value * cos(phase);
          imag440 += value * sin(phase);
          real880 += value * cos(2 * phase);
          imag880 += value * sin(2 * phase);
          real1320 += value * cos(3 * phase);
          imag1320 += value * sin(3 * phase);
        }
        double rms = sqrt(sum / (frames * 2));
        if (rms > peak) {
          peak = rms;
        }
        if (rms > 0.0001) {
          withAudio++;
        }
      }
      cap->ReleaseBuffer(frames);
    }
  }
  client->Stop();
  printf("FREQUENCIES 440Hz=%.8f 880Hz=%.8f 1320Hz=%.8f samples=%llu\n", sample_index ? 2 * hypot(real440, imag440) / sample_index : 0, sample_index ? 2 * hypot(real880, imag880) / sample_index : 0, sample_index ? 2 * hypot(real1320, imag1320) / sample_index : 0, sample_index);
  printf("RESULT buffers_with_audio=%d silent=%d timeouts=%d peak_rms=%.6f\n", withAudio, silent, empty, peak);
  printf("VERDICT: %s\n", withAudio > 0 ? "PROCESS LOOPBACK WORKS" : "no audio captured from this pid tree");
  return withAudio > 0 ? 0 : 3;
}
