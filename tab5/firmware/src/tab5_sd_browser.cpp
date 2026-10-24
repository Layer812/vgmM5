#include "tab5_sd_browser.hpp"
#include <algorithm>
namespace tab5 {
bool valid_directory_path(const std::string& p) {
  if(p.empty() || p[0]!='/' || p.size()>240 || p.find('\\')!=std::string::npos) return false;
  if(p.find("//")!=std::string::npos || p=="/.." || p.find("/../")!=std::string::npos ||
     p.find("/./")!=std::string::npos || (p.size()>2 && p.substr(p.size()-3)=="/..")) return false;
  return true;
}
std::vector<Entry> make_entries(const std::string& dir,const std::vector<SdRow>& rows,std::size_t max_entries){
  std::vector<Entry> out;
  if(!valid_directory_path(dir)) return out;
  if(dir!="/" && max_entries) out.push_back({EntryKind::Parent,".."});
  std::vector<Entry> rest;
  for(const auto& r: rows){
    if(r.name.empty() || r.name=="." || r.name==".." || r.name.find('/')!=std::string::npos || r.name.find('\\')!=std::string::npos) continue;
    if(r.directory) rest.push_back({EntryKind::Folder,r.name});
    else if(Browser::audio(r.name)) rest.push_back({EntryKind::Audio,r.name});
  }
  std::stable_sort(rest.begin(),rest.end(),[](const Entry&a,const Entry&b){
    if(a.kind!=b.kind) return a.kind==EntryKind::Folder;
    return a.name<b.name;
  });
  for(auto &e:rest){if(out.size()>=max_entries)break;out.push_back(std::move(e));}
  return out;
}
}
