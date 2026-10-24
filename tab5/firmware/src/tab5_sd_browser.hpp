#pragma once
#include "tab5_browser_model.hpp"
#include <cstddef>
#include <string>
#include <vector>
namespace tab5 {
struct SdRow { std::string name; bool directory=false; };
// Pure translator; filesystem enumerator lives on the Tab5 side.
std::vector<Entry> make_entries(const std::string& directory,
                                const std::vector<SdRow>& rows,
                                std::size_t max_entries=512);
bool valid_directory_path(const std::string& path);
} // namespace tab5
