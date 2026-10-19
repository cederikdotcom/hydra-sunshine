/**
 * @file tests/unit/platform/test_process_audio.cpp
 * @brief Process capture must advertise agility and reject an absent target.
 */
#ifdef _WIN32
  #include "src/platform/windows/audio_process_loopback.h"

  #include <gtest/gtest.h>

TEST(ProcessAudio, MissingTargetDoesNotActivateSystemAudio) {
  IAudioClient *client = nullptr;
  EXPECT_EQ(platf::audio::activate_process_loopback(0, &client), E_INVALIDARG);
  EXPECT_EQ(client, nullptr);
}

TEST(ProcessAudio, CompletionHandlerAdvertisesAgilityAndOneIdentity) {
  ASSERT_TRUE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
  auto *handler = new platf::audio::process_activation_handler_t;
  IAgileObject *agile = nullptr;
  ASSERT_EQ(handler->QueryInterface(__uuidof(IAgileObject), reinterpret_cast<void **>(&agile)), S_OK);
  ASSERT_NE(agile, nullptr);
  IUnknown *identity = nullptr;
  ASSERT_EQ(agile->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&identity)), S_OK);
  EXPECT_EQ(identity, static_cast<IActivateAudioInterfaceCompletionHandler *>(handler));
  identity->Release();
  agile->Release();
  EXPECT_EQ(handler->Release(), 0);
  CoUninitialize();
}
#endif
