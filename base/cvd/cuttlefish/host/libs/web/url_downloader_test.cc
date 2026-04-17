//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/libs/web/url_downloader.h"

#include <gtest/gtest.h>

#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

TEST(UrlDownloaderTest, ParseUrlScheme_Gcs) {
  auto scheme = ParseUrlScheme("gs://bucket/object");
  ASSERT_TRUE(scheme.ok());
  EXPECT_EQ(*scheme, UrlScheme::kGcs);
}

TEST(UrlDownloaderTest, ParseUrlScheme_GcsWithPath) {
  auto scheme = ParseUrlScheme("gs://my-bucket/path/to/file.zip");
  ASSERT_TRUE(scheme.ok());
  EXPECT_EQ(*scheme, UrlScheme::kGcs);
}

TEST(UrlDownloaderTest, ParseUrlScheme_Https) {
  auto scheme = ParseUrlScheme("https://example.com/file.zip");
  ASSERT_TRUE(scheme.ok());
  EXPECT_EQ(*scheme, UrlScheme::kHttps);
}

TEST(UrlDownloaderTest, ParseUrlScheme_Http) {
  auto scheme = ParseUrlScheme("http://internal-server/file.zip");
  ASSERT_TRUE(scheme.ok());
  EXPECT_EQ(*scheme, UrlScheme::kHttp);
}

TEST(UrlDownloaderTest, ParseUrlScheme_Unsupported) {
  auto scheme = ParseUrlScheme("ftp://server/file");
  EXPECT_FALSE(scheme.ok());
}

TEST(UrlDownloaderTest, ParseUrlScheme_FileScheme) {
  auto scheme = ParseUrlScheme("file:///path/to/file");
  EXPECT_FALSE(scheme.ok());
}

TEST(UrlDownloaderTest, ParseUrlScheme_InvalidUrl) {
  auto scheme = ParseUrlScheme("not-a-url");
  EXPECT_FALSE(scheme.ok());
}

TEST(UrlDownloaderTest, FilenameFromUrl_Simple) {
  EXPECT_EQ(FilenameFromUrl("gs://bucket/path/file.zip"), "file.zip");
}

TEST(UrlDownloaderTest, FilenameFromUrl_Nested) {
  EXPECT_EQ(FilenameFromUrl("gs://bucket/deep/nested/path/artifact.tar.gz"),
            "artifact.tar.gz");
}

TEST(UrlDownloaderTest, FilenameFromUrl_Https) {
  EXPECT_EQ(FilenameFromUrl("https://example.com/builds/image.zip"),
            "image.zip");
}

TEST(UrlDownloaderTest, FilenameFromUrl_WithQueryParams) {
  EXPECT_EQ(FilenameFromUrl("https://example.com/file.zip?token=abc&v=1"),
            "file.zip");
}

TEST(UrlDownloaderTest, FilenameFromUrl_NoPath) {
  EXPECT_EQ(FilenameFromUrl("gs://bucket"), "bucket");
}

TEST(UrlDownloaderTest, FilenameFromUrl_RootFile) {
  EXPECT_EQ(FilenameFromUrl("gs://bucket/file.zip"), "file.zip");
}

TEST(UrlDownloaderTest, FilenameFromUrl_TrailingSlash) {
  // Edge case: URL ends with slash, filename should be empty
  EXPECT_EQ(FilenameFromUrl("gs://bucket/path/"), "");
}

TEST(UrlDownloaderTest, IsGoogleHost_GoogleApis) {
  EXPECT_TRUE(
      UrlDownloader::IsGoogleHost("https://storage.googleapis.com/bucket"));
}

TEST(UrlDownloaderTest, IsGoogleHost_GoogleCom) {
  EXPECT_TRUE(
      UrlDownloader::IsGoogleHost("https://www.google.com/path"));
}

TEST(UrlDownloaderTest, IsGoogleHost_NonGoogle) {
  EXPECT_FALSE(
      UrlDownloader::IsGoogleHost("https://example.com/file.zip"));
}

TEST(UrlDownloaderTest, IsGoogleHost_NonGoogleWithPort) {
  EXPECT_FALSE(
      UrlDownloader::IsGoogleHost("https://example.com:8443/file.zip"));
}

TEST(UrlDownloaderTest, IsGoogleHost_GoogleApisWithPort) {
  EXPECT_TRUE(
      UrlDownloader::IsGoogleHost("https://storage.googleapis.com:443/b"));
}

TEST(UrlDownloaderTest, IsGoogleHost_NoScheme) {
  EXPECT_FALSE(UrlDownloader::IsGoogleHost("storage.googleapis.com/bucket"));
}

TEST(UrlDownloaderTest, IsGoogleHost_HttpScheme) {
  EXPECT_TRUE(
      UrlDownloader::IsGoogleHost("http://storage.googleapis.com/bucket"));
}

}  // namespace
}  // namespace cuttlefish
