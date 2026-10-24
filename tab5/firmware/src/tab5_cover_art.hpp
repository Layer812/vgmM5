#pragma once
// R54: Pure, host-testable SD cover-art filename policy.
// This header does not touch the audio engine or filesystem.
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace tab5 {
enum class CoverFormat { None, Jpeg, Png, Bmp };
struct CoverCandidate {
  std::string name;
  unsigned long bytes = 0;
};
struct CoverSelection {
  std::string name;
  CoverFormat format = CoverFormat::None;
};
inline std::string cover_ascii_lower(std::string s) {
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}
inline std::string cover_base_name(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return path.substr(slash == std::string::npos ? 0 : slash+1);
}
inline std::string cover_stem(const std::string& name) {
  auto base = cover_base_name(name);
  const auto dot = base.find_last_of('.');
  if (dot != std::string::npos && dot > 0) base.erase(dot);
  return cover_ascii_lower(base);
}
inline CoverFormat cover_format(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos) return CoverFormat::None;
  const auto ext = cover_ascii_lower(name.substr(dot));
  if (ext == ".jpg" || ext == ".jpeg" || ext == ".jpe") return CoverFormat::Jpeg;
  if (ext == ".png") return CoverFormat::Png;
  if (ext == ".bmp") return CoverFormat::Bmp;
  return CoverFormat::None;
}
inline CoverSelection select_cover(const std::vector<CoverCandidate>& candidates,
                                  const std::string& track_path,
                                  unsigned long max_bytes = 8ul * 1024ul * 1024ul) {
  const auto track_stem = cover_stem(track_path);
  const std::vector<std::string> priority = {
    "cover", "folder", "front", "album", "albumart", "jacket", "artwork", "ジャケット"
  };
  CoverSelection chosen;
  int best_priority=999999;
  std::string best_sort;
  for (const auto& candidate : candidates) {
    if (candidate.bytes < 32 || candidate.bytes > max_bytes) continue;
    if (candidate.name.empty() || candidate.name == "." || candidate.name == ".." ||
        candidate.name.find('/') != std::string::npos ||
        candidate.name.find('\\') != std::string::npos ||
        candidate.name.size() > 230) continue;
    const CoverFormat format = cover_format(candidate.name);
    if (format == CoverFormat::None) continue;
    const auto name_stem = cover_stem(candidate.name);
    int ranking = 100;
    if (!track_stem.empty() && name_stem == track_stem) ranking = 0;
    else for (std::size_t k = 0; k < priority.size(); ++k)
      if (name_stem == priority[k]) { ranking = 10 + (int)k; break; }
    // Prefer JPEG if multiple extensions of the same preferred name exist.
    const int score = ranking * 4 + (format == CoverFormat::Jpeg ? 0 :
                                      format == CoverFormat::Png ? 1 : 2);
    const auto sort_name = cover_ascii_lower(candidate.name);
    if (score < best_priority || (score == best_priority && sort_name < best_sort)) {
      best_priority = score;
      best_sort = sort_name;
      chosen = {candidate.name, format};
    }
  }
  return chosen;
}
} // namespace tab5
