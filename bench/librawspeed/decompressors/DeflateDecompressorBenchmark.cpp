/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2017 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "decompressors/DeflateDecompressor.h" // for DeflateDecompressor
#include "adt/Point.h"                         // for iPoint2D
#include "bench/Common.h"                      // for areaToRectangle, benc...
#include "common/Common.h"                     // for isAligned
#include "common/RawImage.h"                   // for RawImage, RawImageData
#include "io/Buffer.h"                         // for Buffer
#include <algorithm>                           // for fill_n
#include <cassert>                             // for assert
#include <cstddef>                             // for size_t
#include <cstdint>                             // for uint8_t
#include <memory>                              // for unique_ptr
#include <type_traits>                         // for integral_constant
#include <vector>                              // for vector
#include <zlib.h>                              // for compress, compressBound
#include <benchmark/benchmark.h>               // for State, Benchmark, BEN...

#ifndef NDEBUG
#include <limits> // for numeric_limits
#endif

using rawspeed::Buffer;
using rawspeed::DeflateDecompressor;

template <size_t N> using BPS = std::integral_constant<size_t, N>;
template <int N> using Pf = std::integral_constant<int, N>;

template <typename BPS>
static std::vector<uint8_t> compressChunk(const rawspeed::RawImage& mRaw,
                                          uLong* bufSize) {
  static_assert(BPS::value > 0, "bad bps");
  static_assert(rawspeed::isAligned(BPS::value, 8), "not byte count");

  const uLong uncompressedLength = BPS::value * mRaw->dim.x * mRaw->dim.y / 8UL;
  assert(uncompressedLength > 0);
  assert(uncompressedLength <= std::numeric_limits<Buffer::size_type>::max());

  *bufSize = compressBound(uncompressedLength);
  assert(*bufSize > 0);
  assert(*bufSize <= std::numeric_limits<Buffer::size_type>::max());

  const std::vector<uint8_t> uBuf(uncompressedLength);
  std::vector<uint8_t> cBuf(*bufSize);

  const int err =
      compress(cBuf.data(), bufSize, uBuf.data(), uncompressedLength);
  if (err != Z_OK)
    throw;

  assert(compressBound(uncompressedLength) >= *bufSize);

  return cBuf;
}

template <typename BPS, typename Pf>
static inline void BM_DeflateDecompressor(benchmark::State& state) {
  static_assert(BPS::value > 0, "bad bps");
  static_assert(rawspeed::isAligned(BPS::value, 8), "not byte count");

  const auto dim = areaToRectangle(state.range(0));
  auto mRaw = rawspeed::RawImage::create(dim, rawspeed::RawImageType::F32, 1);

  uLong cBufSize;
  const std::vector<uint8_t> cBuf = compressChunk<BPS>(mRaw, &cBufSize);
  assert(cBufSize > 0);

  Buffer buf(cBuf.data(), cBufSize);
  assert(buf.getSize() == cBufSize);

  int predictor = 0;
  switch (Pf::value) {
  case 1:
    predictor = 3;
    break;
  case 2:
    predictor = 34894;
    break;
  case 4:
    predictor = 34895;
    break;
  default:
    __builtin_unreachable();
    break;
  }

  std::unique_ptr<unsigned char[]> uBuffer; // NOLINT

  for (auto _ : state) {
    DeflateDecompressor d(buf, mRaw, predictor, BPS::value);

    d.decode(&uBuffer, mRaw->dim, mRaw->dim, {0, 0});
  }

  state.SetComplexityN(dim.area());
  state.SetItemsProcessed(state.complexity_length_n() * state.iterations());
  state.SetBytesProcessed(BPS::value * state.items_processed() / 8);
}

static inline void CustomArgs(benchmark::internal::Benchmark* b) {
  if (benchmarkDryRun()) {
    static constexpr int L2dByteSize = 512U * (1U << 10U);
    b->Arg((L2dByteSize / (32 / 8)) / 4);
    return;
  }

  b->RangeMultiplier(2);
// FIXME: appears to not like 1GPix+ buffers
#if 1
  b->Arg(128 << 20);
#else
  b->Range(1, 1023 << 20)->Complexity(benchmark::oN);
#endif
  b->Unit(benchmark::kMillisecond);
}

#define GEN_E(s, f)                                                            \
  BENCHMARK_TEMPLATE(BM_DeflateDecompressor, BPS<s>, Pf<f>)->Apply(CustomArgs);
#define GEN_PFS(s) GEN_E(s, 1) GEN_E(s, 2) GEN_E(s, 4)
#define GEN_PSS() GEN_PFS(16) GEN_PFS(24) GEN_PFS(32)

GEN_PSS()

BENCHMARK_MAIN();
