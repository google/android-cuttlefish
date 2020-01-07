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

#pragma once

#include <string>
#include <vector>

struct SDP {
    explicit SDP();

    int initCheck() const;

    void clear();
    int setTo(const std::string &data);

    // Section 0 is reserved for top-level attributes, section indices >= 1
    // correspond to each media section starting with an "m=" line.
    size_t countSections() const;

    std::vector<std::string>::const_iterator section_begin(
            size_t section) const;

    std::vector<std::string>::const_iterator section_end(
            size_t section) const;

    struct SectionEditor {
        ~SectionEditor();

        SectionEditor &operator<<(std::string_view s);

        void commit();

    private:
        friend struct SDP;

        explicit SectionEditor(SDP *sdp, size_t section);

        SDP *mSDP;
        size_t mSection;

        std::string mBuffer;
    };

    SectionEditor createSection();
    SectionEditor appendToSection(size_t section);

    static void Test();

private:
    int mInitCheck;
    std::vector<std::string> mLines;

    std::vector<size_t> mLineIndexBySection;

    bool mNewSectionEditorActive;

    void getSectionRange(
            size_t section,
            size_t *lineStartIndex,
            size_t *lineStopIndex) const;

    void commitSectionEdit(
            size_t section, const std::vector<std::string> &lines);
};

