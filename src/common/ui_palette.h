#pragma once

namespace dsvr::palette {

struct Rgb8 {
  unsigned char r;
  unsigned char g;
  unsigned char b;
};

inline constexpr Rgb8 background{17, 19, 15};
inline constexpr Rgb8 surface{27, 29, 24};
inline constexpr Rgb8 raised{36, 37, 30};
inline constexpr Rgb8 border{68, 66, 56};
inline constexpr Rgb8 border_bright{102, 94, 73};
inline constexpr Rgb8 text{230, 224, 208};
inline constexpr Rgb8 muted{184, 177, 158};
inline constexpr Rgb8 bone{223, 211, 184};
inline constexpr Rgb8 moss{165, 173, 140};
inline constexpr Rgb8 moss_dark{114, 126, 96};
inline constexpr Rgb8 ember{193, 126, 88};
inline constexpr Rgb8 ember_light{208, 147, 112};
inline constexpr Rgb8 ember_dark{121, 76, 54};
inline constexpr Rgb8 ink{24, 23, 17};

}  // namespace dsvr::palette
