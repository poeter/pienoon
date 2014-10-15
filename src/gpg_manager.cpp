// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "precompiled.h"
#include "gpg_manager.h"

//#define NO_GPG

namespace fpl {

bool GPGManager::Initialize() {
# ifdef NO_GPG
  return true;
# endif
  /*
  // This code is here because we may be able to do this part of the
  // initialization here in the future, rather than relying on JNI_OnLoad below.
  auto env = reinterpret_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
  JavaVM *vm = nullptr;
  auto ret = env->GetJavaVM(&vm);
  assert(ret >= 0);
  gpg::AndroidInitialization::JNI_OnLoad(vm);
  */
  gpg::AndroidPlatformConfiguration platform_configuration;
  platform_configuration.SetActivity((jobject)SDL_AndroidGetActivity());

  // Creates a games_services object that has lambda callbacks.
  state = kStart;
  game_services_ =
    gpg::GameServices::Builder()
      .SetDefaultOnLog(gpg::LogLevel::VERBOSE)
      .SetOnAuthActionStarted([this](gpg::AuthOperation op) {
        state = state == kAuthUILaunched
                ? kAuthUIStarted
                : kAutoAuthStarted;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPG: Auto Sign in started!");
      })
      .SetOnAuthActionFinished([this](gpg::AuthOperation op,
                                      gpg::AuthStatus status) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPG: Auto Sign in finished with a result of %d", status);
        state = status == gpg::AuthStatus::VALID
                ? kAuthed
                : (state == kAuthUIStarted ? kAuthUIFailed : kAutoAuthFailed);
      })
      .Create(platform_configuration);

  if (!game_services_) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                "GPG: failed to create GameServices!");
    return false;
  }

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GPG: created GameServices");
  return true;
}

// Called every frame from the game, to see if there's anything to be done
// with the async progress from gpg
void GPGManager::Update() {
# ifdef NO_GPG
  return;
# endif
  assert(game_services_);
  switch(state) {
    case kStart:
    case kAutoAuthStarted:
      // Nothing to do, waiting.
      break;
    case kAutoAuthFailed:
      // Need to explicitly ask for user  login.
      game_services_->StartAuthorizationUI();
      state = kAuthUILaunched;
      break;
    case kAuthUILaunched:
    case kAuthUIStarted:
      // Nothing to do, waiting.
      break;
    case kAuthUIFailed:
      // Both auto and UI based auth failed, I guess at this point we give up.
      break;
    case kAuthed:
      // We're good. TODO: Now start actually using gpg functionality...
      break;
  }
}

bool GPGManager::LoggedIn() {
# ifdef NO_GPG
  return true;
# endif
  assert(game_services_);
  if (state < kAuthed) {
    SDL_LogWarn(SDL_LOG_CATEGORY_ERROR,
                "GPG: player not logged in, can\'t interact with gpg!");
    return false;
  }
  return true;
}


void GPGManager::SaveStat(const char *event_id, uint64_t *score) {
# ifdef NO_GPG
  return;
# endif
  if (!LoggedIn()) return;
  game_services_->Events().Increment(event_id, *score);
  *score = 0;  // Reset accumulation.
}

void GPGManager::ShowLeaderboards(const GPGIds *ids, size_t id_len) {
# ifdef NO_GPG
  return;
# endif
  if (!LoggedIn()) return;
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GPG: launching leaderboard UI");
  // First, get all current event counts from GPG in one callback,
  // which allows us to conveniently update and show the leaderboards without
  // having to deal with multiple callbacks.
  game_services_->Events().FetchAll([id_len, ids, this](
        const gpg::EventManager::FetchAllResponse &far) {
    for (auto it = far.data.begin(); it != far.data.end(); ++it) {
      // Look up leaderboard id from corresponding event id.
      const char *leaderboard_id = nullptr;
      for (size_t i = 0; i < id_len; i++) {
        if (ids[i].event == it->first) {
          leaderboard_id = ids[i].leaderboard;
        }
      }
      assert(leaderboard_id);
      if (leaderboard_id) {
        game_services_->Leaderboards().SubmitScore(leaderboard_id,
                                                   it->second.Count());
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPG: submitted score %llu for id %s", it->second.Count(),
                    leaderboard_id);
        // TODO: factor this code out into the game, as it is specific to Splat.
        // Also, ideally this should happen during the game.
        if (!strcmp(leaderboard_id, "CgkI97yope0IEAIQAg")) {  // Pies thrown.
          struct Achievement { const char *id; int pie_count; };
          static const Achievement achievements[] = {
            { "CgkI97yope0IEAIQEA", 100 },
            { "CgkI97yope0IEAIQEQ", 250 },
            { "CgkI97yope0IEAIQEg", 1000 },
            { "CgkI97yope0IEAIQEw", 2500 },
            { "CgkI97yope0IEAIQFA", 10000 },
          };
          for (size_t i = 0; i < sizeof(achievements) / sizeof(Achievement);
               i++) {
            if (it->second.Count() >= achievements[i].pie_count) {
              game_services_->Achievements().Unlock(achievements[i].id);
              SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                          "GPG: achievement unlocked: %d pies",
                          achievements[i].pie_count);
            }
          }
        }
      }
    }
    game_services_->Leaderboards().ShowAllUI([](const gpg::UIStatus &status) {
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                  "GPG: UI FAILED, UIStatus is: %d", status);
    });
  });
}

}  // fpl

#ifdef __ANDROID__
jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "main: JNI_OnLoad called");

  gpg::AndroidInitialization::JNI_OnLoad(vm);

  return JNI_VERSION_1_4;
}

extern "C" JNIEXPORT void JNICALL
  Java_com_google_fpl_splat_FPLActivity_nativeOnActivityResult(
    JNIEnv *env,
    jobject thiz,
    jobject activity,
    jint request_code,
    jint result_code,
    jobject data) {
  gpg::AndroidSupport::OnActivityResult(
      env, activity, request_code, result_code, data);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GPG: nativeOnActivityResult");
}
#endif