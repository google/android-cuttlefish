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

