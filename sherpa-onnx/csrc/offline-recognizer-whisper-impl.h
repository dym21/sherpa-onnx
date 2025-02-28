// sherpa-onnx/csrc/offline-recognizer-whisper-impl.h
//
// Copyright (c)  2022-2023  Xiaomi Corporation

#ifndef SHERPA_ONNX_CSRC_OFFLINE_RECOGNIZER_WHISPER_IMPL_H_
#define SHERPA_ONNX_CSRC_OFFLINE_RECOGNIZER_WHISPER_IMPL_H_

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if __ANDROID_API__ >= 9
#include "android/asset_manager.h"
#include "android/asset_manager_jni.h"
#endif

#include "sherpa-onnx/csrc/offline-model-config.h"
#include "sherpa-onnx/csrc/offline-recognizer-impl.h"
#include "sherpa-onnx/csrc/offline-recognizer.h"
#include "sherpa-onnx/csrc/offline-whisper-decoder.h"
#include "sherpa-onnx/csrc/offline-whisper-greedy-search-decoder.h"
#include "sherpa-onnx/csrc/offline-whisper-model.h"
#include "sherpa-onnx/csrc/symbol-table.h"
#include "sherpa-onnx/csrc/transpose.h"

namespace sherpa_onnx {

static std::string FixInvalidUtf8(const std::string &s) {
  int32_t s_size = s.size();

  std::string ans;
  ans.reserve(s_size);

  for (int32_t i = 0; i < s_size;) {
    uint8_t c = s[i];
    if (c < 0x80) {
      // valid
      ans.append(1, c);
      ++i;
      continue;
    } else if ((c >= 0xc0) && (c < 0xe0)) {
      // beginning of two bytes
      if ((i + 1) > (s_size - 1)) {
        // no subsequent byte. invalid!
        i += 1;
        continue;
      }
      uint8_t next = s[i + 1];
      if (!(next >= 0x80 && next < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }
      // valid 2-byte utf-8
      ans.append(1, c);
      ans.append(1, next);
      i += 2;
      continue;
    } else if ((c >= 0xe0) && (c < 0xf0)) {
      // beginning of 3 bytes
      if ((i + 2) > (s_size - 1)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next = s[i + 1];
      if (!(next >= 0x80 && next < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next2 = s[i + 2];
      if (!(next2 >= 0x80 && next2 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      ans.append(1, c);
      ans.append(1, next);
      ans.append(1, next2);
      i += 3;
      continue;
    } else if ((c >= 0xf0) && (c < 0xf8)) {
      // 4 bytes
      if ((i + 3) > (s_size - 1)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next = s[i + 1];
      if (!(next >= 0x80 && next < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next2 = s[i + 2];
      if (!(next2 >= 0x80 && next2 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next3 = s[i + 3];
      if (!(next3 >= 0x80 && next3 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }
      ans.append(1, c);
      ans.append(1, next);
      ans.append(1, next2);
      ans.append(1, next3);
      i += 4;
      continue;
    } else if ((c >= 0xf8) && (c < 0xfc)) {
      // 5 bytes
      if ((i + 4) > (s_size - 1)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next = s[i + 1];
      if (!(next >= 0x80 && next < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next2 = s[i + 2];
      if (!(next2 >= 0x80 && next2 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next3 = s[i + 3];
      if (!(next3 >= 0x80 && next3 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next4 = s[i + 4];
      if (!(next4 >= 0x80 && next4 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }
      ans.append(1, c);
      ans.append(1, next);
      ans.append(1, next2);
      ans.append(1, next3);
      ans.append(1, next4);
      i += 5;
      continue;
    } else if ((c >= 0xfc) && (c < 0xfe)) {
      // 6 bytes
      if ((i + 5) > (s_size - 1)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next = s[i + 1];
      if (!(next >= 0x80 && next < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next2 = s[i + 2];
      if (!(next2 >= 0x80 && next2 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next3 = s[i + 3];
      if (!(next3 >= 0x80 && next3 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next4 = s[i + 4];
      if (!(next4 >= 0x80 && next4 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }

      uint8_t next5 = s[i + 5];
      if (!(next5 >= 0x80 && next5 < 0xc0)) {
        // invalid
        i += 1;
        continue;
      }
      ans.append(1, c);
      ans.append(1, next);
      ans.append(1, next2);
      ans.append(1, next3);
      ans.append(1, next4);
      ans.append(1, next5);
      i += 6;
      continue;
    } else {
      i += 1;
    }
  }
  return ans;
}

static OfflineRecognitionResult Convert(const OfflineWhisperDecoderResult &src,
                                        const SymbolTable &sym_table) {
  OfflineRecognitionResult r;
  r.tokens.reserve(src.tokens.size());

  std::string text;
  for (auto i : src.tokens) {
    if (!sym_table.contains(i)) {
      continue;
    }

    const auto &s = sym_table[i];
    text += s;
    r.tokens.push_back(s);
  }

  // TODO(fangjun): Fix the following error in offline-stream.cc
  //
  //      j["text"] = text;

  // libc++abi: terminating with uncaught exception of type
  // nlohmann::json_abi_v3_11_2::detail::type_error:
  // [json.exception.type_error.316] incomplete UTF-8 string; last byte: 0x86

#if 0
  r.text = FixInvalidUtf8(text);
#else
  r.text = text;
#endif

  return r;
}

class OfflineRecognizerWhisperImpl : public OfflineRecognizerImpl {
 public:
  explicit OfflineRecognizerWhisperImpl(const OfflineRecognizerConfig &config)
      : config_(config),
        symbol_table_(config_.model_config.tokens),
        model_(std::make_unique<OfflineWhisperModel>(config.model_config)) {
    Init();
  }

#if __ANDROID_API__ >= 9
  OfflineRecognizerWhisperImpl(AAssetManager *mgr,
                               const OfflineRecognizerConfig &config)
      : config_(config),
        symbol_table_(mgr, config_.model_config.tokens),
        model_(
            std::make_unique<OfflineWhisperModel>(mgr, config.model_config)) {
    Init();
  }

#endif

  void Init() {
    // tokens.txt from whisper is base64 encoded, so we need to decode it
    symbol_table_.ApplyBase64Decode();

    if (config_.decoding_method == "greedy_search") {
      decoder_ = std::make_unique<OfflineWhisperGreedySearchDecoder>(
          config_.model_config.whisper, model_.get());
    } else {
      SHERPA_ONNX_LOGE(
          "Only greedy_search is supported at present for whisper. Given %s",
          config_.decoding_method.c_str());
      exit(-1);
    }
  }

  std::unique_ptr<OfflineStream> CreateStream() const override {
    return std::make_unique<OfflineStream>(WhisperTag{});
  }

  void DecodeStreams(OfflineStream **ss, int32_t n) const override {
    // batch decoding is not implemented yet
    for (int32_t i = 0; i != n; ++i) {
      DecodeStream(ss[i]);
    }
  }

 private:
  void DecodeStream(OfflineStream *s) const {
    int32_t max_num_frames = 3000;
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    int32_t feat_dim = s->FeatureDim();
    std::vector<float> f = s->GetFrames();
    int32_t num_frames = f.size() / feat_dim;

    if (num_frames > max_num_frames) {
      SHERPA_ONNX_LOGE("Only waves less than 30 seconds are supported.");
      exit(-1);
    }

    NormalizeFeatures(f.data(), num_frames, feat_dim);

    std::array<int64_t, 3> shape{1, max_num_frames, feat_dim};

    Ort::Value mel = Ort::Value::CreateTensor<float>(
        model_->Allocator(), shape.data(), shape.size());
    float *p_mel = mel.GetTensorMutableData<float>();
    std::copy(f.begin(), f.end(), p_mel);

    memset(p_mel + f.size(), 0,
           (max_num_frames - num_frames) * feat_dim * sizeof(float));
    mel = Transpose12(model_->Allocator(), &mel);

    auto cross_kv = model_->ForwardEncoder(std::move(mel));

    auto results =
        decoder_->Decode(std::move(cross_kv.first), std::move(cross_kv.second));

    auto r = Convert(results[0], symbol_table_);
    s->SetResult(r);
  }

 private:
  static void NormalizeFeatures(float *features, int32_t num_frames,
                                int32_t feat_dim) {
    // log_spec = torch.clamp(features, min=1e-10).log10()
    // log_spec = torch.maximum(log_spec, log_spec.max() - 8.0)
    // mel = (log_spec + 4.0) / 4.0

    int32_t n = num_frames * feat_dim;
    float max_v = -1e20;
    for (int32_t i = 0; i != n; ++i) {
      float f = features[i];

      f = std::max<float>(f, 1e-10);
      f = std::log10(f);

      max_v = std::max(f, max_v);

      features[i] = f;
    }

    max_v -= 8;

    for (int32_t i = 0; i != n; ++i) {
      float f = features[i];
      f = std::max(f, max_v);

      f = (f + 4) / 4;

      features[i] = f;
    }
  }

 private:
  OfflineRecognizerConfig config_;
  SymbolTable symbol_table_;
  std::unique_ptr<OfflineWhisperModel> model_;
  std::unique_ptr<OfflineWhisperDecoder> decoder_;
};

}  // namespace sherpa_onnx

#endif  // SHERPA_ONNX_CSRC_OFFLINE_RECOGNIZER_WHISPER_IMPL_H_
