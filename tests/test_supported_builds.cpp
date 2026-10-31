// The supported-build table is the only thing standing between a Steam patch
// and DSVR reading a layout it never qualified against. These checks pin the
// table's shape and the all-or-nothing identity match.
#include "../src/common/version.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void Expect(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what);
  }
}

}  // namespace

int main() {
  Expect(dsvr::kSupportedBuildCount >= 1, "at least one supported build");
  for (unsigned i = 0; i < dsvr::kSupportedBuildCount; ++i) {
    const dsvr::SupportedBuild& b = dsvr::kSupportedBuilds[i];
    Expect(b.file_size > 0ull, "row has a file size");
    Expect(b.time_date_stamp != 0, "row has a PE timestamp");
    Expect(b.size_of_image > 0, "row has an image size");
    Expect(b.sha256 && std::strlen(b.sha256) == 64, "row has a 64-hex sha256");
    Expect(b.label && *b.label, "row has a player-facing label");
    for (unsigned j = 0; j < i; ++j)
      Expect(std::strcmp(b.sha256, dsvr::kSupportedBuilds[j].sha256) != 0,
             "no two rows share a hash");
  }

  const dsvr::SupportedBuild& pinned = dsvr::kPinnedBuild;
  Expect(std::strcmp(pinned.sha256, dsvr::kExpectedSha256) == 0,
         "pinned row matches the legacy sha constant");
  Expect(pinned.file_size == dsvr::kExpectedFileSize,
         "pinned row matches the legacy size constant");
  Expect(pinned.time_date_stamp == dsvr::kExpectedTimeDateStamp,
         "pinned row matches the legacy timestamp constant");
  Expect(pinned.size_of_image == dsvr::kExpectedSizeOfImage,
         "pinned row matches the legacy image-size constant");

  Expect(dsvr::FindSupportedBuild(pinned.file_size, pinned.time_date_stamp,
                                  pinned.size_of_image, pinned.sha256) == 0,
         "exact identity matches");
  // Any single fact changed is a different build: none of these may match.
  Expect(dsvr::FindSupportedBuild(pinned.file_size + 1, pinned.time_date_stamp,
                                  pinned.size_of_image, pinned.sha256) < 0,
         "size drift is refused");
  Expect(dsvr::FindSupportedBuild(pinned.file_size, pinned.time_date_stamp + 1,
                                  pinned.size_of_image, pinned.sha256) < 0,
         "timestamp drift is refused");
  Expect(dsvr::FindSupportedBuild(pinned.file_size, pinned.time_date_stamp,
                                  pinned.size_of_image + 0x1000,
                                  pinned.sha256) < 0,
         "image-size drift is refused");
  Expect(dsvr::FindSupportedBuild(pinned.file_size, pinned.time_date_stamp,
                                  pinned.size_of_image, "0000000000000000") < 0,
         "hash drift is refused");
  Expect(dsvr::FindSupportedBuild(pinned.file_size, pinned.time_date_stamp,
                                  pinned.size_of_image, nullptr) < 0,
         "a missing hash is refused");
  Expect(dsvr::FindSupportedBuild(1ull, 1ul, 1ul, pinned.sha256) < 0,
         "an unrelated exe is refused");

  if (failures) {
    std::printf("test_supported_builds: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("test_supported_builds: PASS\n");
  return 0;
}
