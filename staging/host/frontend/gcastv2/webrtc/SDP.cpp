/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <webrtc/SDP.h>

#include "Utils.h"

#include <android-base/logging.h>

#include <cerrno>
#include <iostream>

SDP::SDP()
    : mInitCheck(-ENODEV),
      mNewSectionEditorActive(false) {
}

int SDP::initCheck() const {
    return mInitCheck;
}

size_t SDP::countSections() const {
    CHECK(!mInitCheck);
    return mLineIndexBySection.size();
}

void SDP::clear() {
    mInitCheck = -ENODEV;
    mLines.clear();
    mLineIndexBySection.clear();
}

int SDP::setTo(const std::string &data) {
    clear();

    mLines = SplitString(data, "\r\n");

    LOG(VERBOSE) << "SDP contained " << mLines.size() << " lines.";

    mLineIndexBySection.push_back(0);

    mInitCheck = 0;

    for (size_t i = 0; i < mLines.size(); ++i) {
        const auto &line = mLines[i];

        LOG(VERBOSE) << "Line #" << i << ": " << line;

        if (i == 0 && line != "v=0") {
            mInitCheck = -EINVAL;
            break;
        }

        if (line.size() < 2 || line[1] != '=') {
            mInitCheck = -EINVAL;
            break;
        }

        if (line[0] == 'm') {
            mLineIndexBySection.push_back(i);
        }
    }

    return mInitCheck;
}

void SDP::getSectionRange(
        size_t section, size_t *lineStartIndex, size_t *lineStopIndex) const {
    CHECK(!mInitCheck);
    CHECK_LT(section, mLineIndexBySection.size());

    if (lineStartIndex) {
        *lineStartIndex = mLineIndexBySection[section];
    }

    if (lineStopIndex) {
        if (section + 1 < mLineIndexBySection.size()) {
            *lineStopIndex = mLineIndexBySection[section + 1];
        } else {
            *lineStopIndex = mLines.size();
        }
    }
}

std::vector<std::string>::const_iterator SDP::section_begin(
        size_t section) const {

    size_t startLineIndex;
    getSectionRange(section, &startLineIndex, nullptr /* lineStopIndex */);

    return mLines.cbegin() + startLineIndex;
}

std::vector<std::string>::const_iterator SDP::section_end(
        size_t section) const {

    size_t stopLineIndex;
    getSectionRange(section, nullptr /* lineStartIndex */, &stopLineIndex);

    return mLines.cbegin() + stopLineIndex;
}

SDP::SectionEditor SDP::createSection() {
    CHECK(!mNewSectionEditorActive);
    mNewSectionEditorActive = true;

    if (mInitCheck) {
        clear();
        mInitCheck = 0;
    }

    return SectionEditor(this, countSections());
}

SDP::SectionEditor SDP::appendToSection(size_t section) {
    CHECK_LT(section, countSections());
    return SectionEditor(this, section);
}

void SDP::commitSectionEdit(
        size_t section, const std::vector<std::string> &lines) {

    CHECK_LE(section, countSections());

    if (section == countSections()) {
        // This was an edit creating a new section.
        mLineIndexBySection.push_back(mLines.size());

        mLines.insert(mLines.end(), lines.begin(), lines.end());

        mNewSectionEditorActive = false;
        return;
    }

    mLines.insert(section_end(section), lines.begin(), lines.end());

    if (section + 1 < countSections()) {
        mLineIndexBySection[section + 1] += lines.size();
    }
}

////////////////////////////////////////////////////////////////////////////////

SDP::SectionEditor::SectionEditor(SDP *sdp, size_t section)
    : mSDP(sdp),
      mSection(section) {
}

SDP::SectionEditor::~SectionEditor() {
    commit();
}

SDP::SectionEditor &SDP::SectionEditor::operator<<(std::string_view s) {
    mBuffer.append(s);

    return *this;
}

void SDP::SectionEditor::commit() {
    if (mSDP) {
        auto lines = SplitString(mBuffer, "\r\n");

        mSDP->commitSectionEdit(mSection, lines);
        mSDP = nullptr;
    }
}
