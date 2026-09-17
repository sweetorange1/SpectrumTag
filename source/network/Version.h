// ==========================================================
// source/network/Version.h
//   语义化版本号（SemVer）解析与比较工具。
//   用于客户端将本地版本与服务端返回的最新版本做大小对比，
//   以决策是否弹窗提示更新（服务端 has_update 的本地兜底校验）。
//
//   支持格式：X.Y.Z[-prerelease][+build]
//   比较规则：先比 major → minor → patch；
//   正式版 > 预发布版；预发布版之间按字典序。
// ==========================================================

#pragma once

#include <juce_core/juce_core.h>

namespace spectrumtag {
namespace network {

// 表示一个解析后的语义化版本
struct SemVer {
  int major = 0;
  int minor = 0;
  int patch = 0;
  juce::String prerelease;  // e.g. "beta.1"，为空表示正式版
  juce::String build;       // 构建元数据，不参与比较

  // 从 "1.2.0" / "1.3.0-beta.1" 等字符串解析，失败时 major=-1
  static SemVer Parse(const juce::String& raw);

  // 转为完整字符串 "X.Y.Z" 或 "X.Y.Z-prerelease"
  juce::String ToString() const;

  // 是否解析成功
  bool IsValid() const noexcept { return major >= 0; }
};

// 比较 a 和 b：
//   返回 <0 表示 a < b
//   返回  0 表示 a == b
//   返回 >0 表示 a > b
int CompareVersions(const SemVer& a, const SemVer& b);

// 便捷重载：直接接收字符串
inline int CompareVersionStrings(const juce::String& a, const juce::String& b) {
  return CompareVersions(SemVer::Parse(a), SemVer::Parse(b));
}

}  // namespace network
}  // namespace spectrumtag
