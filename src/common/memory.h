#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dsvr {

// Short native-owner memory transaction: reuse region permission checks, never
// data. End the scope (or Invalidate it) before invoking native code that may
// change mappings. Guarded writes invalidate it around page protection changes.
// SEH guards every copy; read/write faults invalidate the entire scope chain.
// Unchanged nested scopes restore their parent's cache. This is not game-thread
// synchronization: nothing is cached across callbacks or shared between threads.
class GuardedReadScope {
 public:
  GuardedReadScope():previous_(current_) { current_=this; }
  ~GuardedReadScope() { current_=previous_; }
  GuardedReadScope(const GuardedReadScope&)=delete;
  GuardedReadScope& operator=(const GuardedReadScope&)=delete;
  static void Invalidate() noexcept {
    // A nested copy fault also disproves suspended parents' page metadata.
    for(auto* scope=current_;scope;scope=scope->previous_)scope->count_=0;
  }
  static bool Contains(std::uintptr_t start,std::uintptr_t end,bool writable=false) {
    if(!current_)return false;
    for(unsigned i=0;i<current_->count_;++i) {
      const auto& range=current_->ranges_[i];
      if(start>=range.start && end<=range.end && (!writable || range.writable))return true;
    }
    return false;
  }
  static void Remember(std::uintptr_t start,std::uintptr_t end,bool writable=false) {
    if(!current_)return;
    for(unsigned i=0;i<current_->count_;++i) {
      auto& range=current_->ranges_[i];
      if(range.start==start && range.end==end) {range.writable=writable;return;}
    }
    if(current_->count_<current_->ranges_.size())
      current_->ranges_[current_->count_++]={start,end,writable};
  }
 private:
  struct Range {std::uintptr_t start=0,end=0;bool writable=false;};
  std::array<Range,16> ranges_{};
  unsigned count_=0;
  GuardedReadScope* previous_=nullptr;
  static inline thread_local GuardedReadScope* current_=nullptr;
};

// Base + size of a loaded module (game exe or a DLL).
std::uintptr_t ModuleBase(const wchar_t* module_name_or_null);
std::size_t ModuleSize(std::uintptr_t base);

// Guarded read/write. Returns false instead of crashing on bad memory.
// Reads use VirtualQuery + SEH; writes require the caller to hold a proper
// game-thread context (see docs: never write from a foreign thread).
bool GuardedReadBytes(std::uintptr_t address, void* out, std::size_t size);
template <typename T>
bool GuardedRead(std::uintptr_t address, T& out) {
  return GuardedReadBytes(address, &out, sizeof(T));
}
bool GuardedWriteBytes(std::uintptr_t address, const void* data,
                       std::size_t size);

// Follow a pointer chain: base -> +off0 -> +off1 ... . Each step is guarded.
// Returns nullopt on any bad link. Offsets are in bytes.
std::optional<std::uintptr_t> FollowChain(
    std::uintptr_t base, const std::uintptr_t* offsets, std::size_t count);

// Byte-pattern scan over executable sections of a module.
// Pattern syntax: "48 8B ? ? 90" (? = wildcard nibble or byte). Returns RVAs.
std::vector<std::uintptr_t> PatternScan(std::uintptr_t module_base,
                                        const char* pattern);

// Verify that live bytes equal expected (for refusing to hook wrong builds).
bool BytesMatch(std::uintptr_t address, const void* expected, std::size_t size);

// SHA-256 of a file, lowercase hex. Used to pin the exact game build.
bool FileSha256(const wchar_t* path, std::string& out_hex);

// Current exe path (the game when loaded in-process).
std::wstring CurrentExePath();

}  // namespace dsvr
