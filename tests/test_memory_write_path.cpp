#include <windows.h>
#include <cstdio>
#include <thread>
#include <cstring>
namespace {
unsigned protects=0;
unsigned queries=0;
auto native_query=&VirtualQuery;
SIZE_T WINAPI CountQuery(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T size) {
  ++queries;return native_query(address,info,size);
}
auto native_protect=&VirtualProtect;
BOOL WINAPI CountProtect(LPVOID address,SIZE_T size,DWORD protection,PDWORD previous) {
  ++protects;return native_protect(address,size,protection,previous);
}
}
#define VirtualProtect CountProtect
#define VirtualQuery CountQuery
#include "../src/common/memory.cpp"
#undef VirtualProtect
#undef VirtualQuery
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}} while(0)
int CheckExecuteOnlyRead() {
  auto* page=static_cast<int*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  CHECK(page);*page=17;DWORD old=0;
  CHECK(native_protect(page,4096,PAGE_EXECUTE,&old));
  int snapshot=0;
  {
    dsvr::GuardedReadScope scope;
    CHECK(!dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
  }
  CHECK(native_protect(page,4096,PAGE_READWRITE,&old));
  CHECK(VirtualFree(page,0,MEM_RELEASE));
  return 0;
}
int CheckFaultCacheInvalidation() {
  auto* page=static_cast<int*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  CHECK(page);*page=23;DWORD old=0;int snapshot=0;
  {
    dsvr::GuardedReadScope outer;
    CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
    {
      dsvr::GuardedReadScope inner;
      CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
      CHECK(native_protect(page,4096,PAGE_NOACCESS,&old));
      CHECK(!dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
      CHECK(native_protect(page,4096,PAGE_READWRITE|PAGE_GUARD,&old));
      CHECK(!dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
      MEMORY_BASIC_INFORMATION region{};
      CHECK(VirtualQuery(page,&region,sizeof(region)) && (region.Protect&PAGE_GUARD));
    }
    // A fault also disproves any suspended parent's cached page metadata.
    CHECK(!dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
    MEMORY_BASIC_INFORMATION region{};
    CHECK(VirtualQuery(page,&region,sizeof(region)) && (region.Protect&PAGE_GUARD));
  }
  CHECK(native_protect(page,4096,PAGE_READWRITE,&old));
  CHECK(VirtualFree(page,0,MEM_RELEASE));
  return 0;
}
int CheckScopedWrites() {
  auto* page=static_cast<int*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  CHECK(page);
  queries=protects=0;
  {
    dsvr::GuardedReadScope scope;
    for(int i=0;i<100;++i) {
      CHECK(dsvr::GuardedWriteBytes(reinterpret_cast<std::uintptr_t>(page+i),&i,sizeof(i)));
      CHECK(page[i]==i);
    }
    std::printf("scoped 100 pose/camera writes: queries=%u protects=%u (expected 1/0)\n",queries,protects);
    CHECK(queries==1 && protects==0);
  }
  int value=7;
  CHECK(dsvr::GuardedWriteBytes(reinterpret_cast<std::uintptr_t>(page),&value,sizeof(value)));
  CHECK(queries==2); // scope lifetime never leaks permission metadata
  CHECK(VirtualFree(page,0,MEM_RELEASE));
  return 0;
}
int main(int argc,char** argv) {
  if(argc>1 && std::strcmp(argv[1],"execute-only")==0)return CheckExecuteOnlyRead();
  if(argc>1 && std::strcmp(argv[1],"fault-cache")==0)return CheckFaultCacheInvalidation();
  if(argc>1 && std::strcmp(argv[1],"scoped-writes")==0)return CheckScopedWrites();
  auto* page=static_cast<int*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  CHECK(page);const int value=42;
  // A pose callback reads the same native allocation repeatedly. Each read
  // remains a fresh copy, while protection discovery is bounded to this scope.
  queries=0;
  {
    dsvr::GuardedReadScope scope;
    int snapshot=0;
    for(int i=0;i<100;++i) {
      *page=i;
      CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
      CHECK(snapshot==i);
    }
    CHECK(queries==1);
  }
  int snapshot=0;
  CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
  CHECK(queries==2); // no stale permissions survive the callback
  {
    dsvr::GuardedReadScope outer;
    CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
    const auto outer_queries=queries;
    {
      dsvr::GuardedReadScope inner;
      CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
      CHECK(queries==outer_queries+1);
    }
    CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
    CHECK(queries==outer_queries+1); // outer cache restored after nested operation
    bool copied=false;
    std::thread reader([&]{int local=0;copied=dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),local);});
    reader.join();CHECK(copied && queries==outer_queries+2); // no inherited TLS cache
    CHECK(!dsvr::GuardedReadBytes(~std::uintptr_t(0)-1,&snapshot,sizeof(snapshot)));
  }
  // The fixed cache must fall back to querying when more allocations are read.
  std::array<void*,20> allocations{};
  for(auto& allocation:allocations) {
    allocation=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);CHECK(allocation);
  }
  {
    dsvr::GuardedReadScope scope;
    for(auto allocation:allocations)CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(allocation),snapshot));
    const auto full_queries=queries;
    CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(allocations.back()),snapshot));
    CHECK(queries==full_queries+1);
  }
  for(auto allocation:allocations)CHECK(VirtualFree(allocation,0,MEM_RELEASE));
  for(int i=0;i<100;++i)CHECK(dsvr::GuardedWriteBytes(reinterpret_cast<std::uintptr_t>(page),&value,sizeof(value)));
  CHECK(*page==42 && protects==0); // ordinary pose/camera data must not reprotect heap pages
  DWORD old=0;CHECK(native_protect(page,4096,PAGE_READONLY,&old));
  CHECK(dsvr::GuardedWriteBytes(reinterpret_cast<std::uintptr_t>(page),&value,sizeof(value)));
  CHECK(protects==2);
  MEMORY_BASIC_INFORMATION region{};CHECK(VirtualQuery(page,&region,sizeof(region)) && region.Protect==PAGE_READONLY);
  CHECK(native_protect(page,4096,PAGE_READWRITE|PAGE_GUARD,&old));
  const auto before=protects;int out=0;
  {
    dsvr::GuardedReadScope scope;
    CHECK(!dsvr::GuardedReadBytes(reinterpret_cast<std::uintptr_t>(page),&out,sizeof(out)));
  }
  CHECK(!dsvr::GuardedWriteBytes(reinterpret_cast<std::uintptr_t>(page),&value,sizeof(value)));
  CHECK(!dsvr::GuardedReadBytes(reinterpret_cast<std::uintptr_t>(page),&out,sizeof(out)));
  CHECK(protects==before && VirtualQuery(page,&region,sizeof(region)) && (region.Protect&PAGE_GUARD));
  CHECK(native_protect(page,4096,PAGE_READWRITE,&old));
  auto* tail=reinterpret_cast<unsigned char*>(page)+4096;
  CHECK(native_protect(tail,4096,PAGE_NOACCESS,&old));
  {
    dsvr::GuardedReadScope scope;
    std::uint64_t crossing=0;
    CHECK(!dsvr::GuardedReadBytes(reinterpret_cast<std::uintptr_t>(tail)-4,&crossing,sizeof(crossing)));
    CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(page),snapshot));
  }
  CHECK(native_protect(tail,4096,PAGE_READWRITE,&old));
  // A concurrent decommit after discovery must still be caught by the copy's SEH.
  {
    dsvr::GuardedReadScope scope;
    CHECK(dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(tail),snapshot));
    CHECK(VirtualFree(tail,4096,MEM_DECOMMIT));
    CHECK(!dsvr::GuardedRead(reinterpret_cast<std::uintptr_t>(tail),snapshot));
  }
  CHECK(!dsvr::GuardedWriteBytes(~std::uintptr_t(0)-1,&value,sizeof(value)));
  CHECK(VirtualFree(page,0,MEM_RELEASE));
  CHECK(CheckExecuteOnlyRead()==0);
  CHECK(CheckFaultCacheInvalidation()==0);
  CHECK(CheckScopedWrites()==0);
  std::puts("PASS actual guarded memory writes: zero heap reprotection, protected restoration, untouched guards");
}
