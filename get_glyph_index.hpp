/*
//  Copyright (c) 2020 Peter Frane Jr. All Rights Reserved.
//
//  Use of this source code is governed by the GNU LESSER GENERAL PUBLIC LICENSE v. 2.1 license that can be
//  found in the LICENSE file.
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
//  OF ANY KIND, either express or implied.
//
//  For inquiries, email the author at pfranejr AT hotmail.com
*/

#pragma once
#include <iostream>
#include <string>
#include <string.h>
#include <Windows.h>
#include <stdexcept>

using namespace std;

typedef uint16_t uint16;
typedef uint32_t uint32;

class unicode_to_glyph_index
{
private:
    struct EncodingRecord
    {
        uint16 platformID;
        uint16 encodingID;
        uint32 offset;
    };
    struct SequentialMapGroup
    {
        uint32 startCharCode;
        uint32 endCharCode;
        uint32 startGlyphID;
    };
    struct cmap_subtable_format_12
    {
        uint16 format;
        uint16 reserved;
        uint32 length;
        uint32 language;
        uint32 numGroups;
        SequentialMapGroup groups[1];
    };

private:
    HDC m_hdc;
    HFONT m_hfont;
    bool m_error;
    string m_error_message;
    unsigned char* m_cmap_table;
    SequentialMapGroup* m_map_group;
    uint32 m_map_size;

    //unicode_to_glyph_index(){}
    //unicode_to_glyph_index(const unicode_to_glyph_index &ci){}
    //unicode_to_glyph_index& operator=(const unicode_to_glyph_index& src) { return *this; }
    void load_font(const char* facename, bool bold, bool italic)
    {
        LOGFONTA lf = { 0 };

        lf.lfCharSet = DEFAULT_CHARSET;

        if (bold)
            lf.lfWeight = FW_BOLD;

        if (italic)
            lf.lfItalic = 1;

        strcpy_s(lf.lfFaceName, sizeof(lf.lfFaceName), facename);

        m_hfont = CreateFontIndirectA(&lf);

        if (!m_hfont)
        {
            string msg("Unable to load font: ");

            msg += facename;

            throw runtime_error(msg);
        }
        else
        {
            HDC hdc = GetDC(HWND_DESKTOP);

            m_hdc = CreateCompatibleDC(hdc);

            SaveDC(m_hdc);

            SelectObject(m_hdc, m_hfont);

            ReleaseDC(HWND_DESKTOP, hdc);
        }
    }
    void read_cmap_table()
    {
        const DWORD table_ID = MAKELONG(MAKEWORD('c', 'm'), MAKEWORD('a', 'p'));
        DWORD table_size;

        table_size = GetFontData(m_hdc, table_ID, 0, nullptr, 0);

        if (GDI_ERROR == table_size)
        {
            throw runtime_error("'cmap' table not found");
        }
        else
        {
            m_cmap_table = new BYTE[table_size];

            if (!m_cmap_table)
            {
                throw runtime_error("Unable to allocate memory for the 'cmap' table");
            }
            else
            {
                if (GetFontData(m_hdc, table_ID, 0, m_cmap_table, table_size) == GDI_ERROR)
                {
                    delete[] m_cmap_table;

                    m_cmap_table = nullptr;

                    throw runtime_error("Error reading 'cmap' table");
                }
            }
        }
    }
    uint16 read_word(uint16 w)
    {
        return _byteswap_ushort(w);
    }
    uint32 read_dword(uint32 dw)
    {
        return _byteswap_ulong(dw);
    }
    // see the heading "'cmap' Header" in https://docs.microsoft.com/en-us/typography/opentype/spec/cmap
    // for the format of the cmap Header
    //
    // uint16	version: Table version number (0)
    // uint16	numTables	Number of encoding tables that follow.
    // EncodingRecord	encodingRecords[numTables]
    //
    // NOTE: All integers are in Big-Endian format and should be converted to Little-Endian format
    //
    void parse_cmap_subtable()
    {
        char buf[128];
        uint16 version;
        uint16* pw = (uint16*)m_cmap_table;

        // the first WORD is the version number and must be 0

        version = read_word(pw[0]);

        if (version != 0)
        {
            sprintf_s(buf, sizeof(buf), "Invalid version number: %u. Expected value is 0", version);

            throw runtime_error(buf);
        }
        else
        {
            EncodingRecord* er;
            uint16 numTables;

            // the second word is the number of sub tables

            numTables = read_word(pw[1]);

            // after the previous 2 WORDs is an array of 'EncodingRecord'

            er = (EncodingRecord*)&pw[2];

            for (uint16 i = 0; i < numTables; ++i, ++er)
            {
                er->platformID = read_word(er->platformID);
                er->encodingID = read_word(er->encodingID);

                // We're looking for a subtable with a format number of 12.
                // Its platformID should be 3 and the encodingID should be 10; 
                // See the heading "Format 12: Segmented coverage" in https://docs.microsoft.com/en-us/typography/opentype/spec/cmap

                if (3 == er->platformID && 10 == er->encodingID)
                {
                    cmap_subtable_format_12* csf12;

                    er->offset = read_dword(er->offset);

                    // go to the offset; it's from the beginning of the cmap table

                    csf12 = (cmap_subtable_format_12*)(m_cmap_table + er->offset);

                    // format should be 12

                    csf12->format = read_word(csf12->format);

                    if (csf12->format != 12)
                    {
                        sprintf_s(buf, sizeof(buf), "Invalid format number: %u. Expected is 12", csf12->format);

                        throw runtime_error(buf);
                    }

                    // length of this subtable; we don't need it actually, but let's convert its value to Little Endian anyway

                    csf12->length = read_dword(csf12->length);

                    csf12->numGroups = read_dword(csf12->numGroups);

                    m_map_group = new SequentialMapGroup[csf12->numGroups];

                    if (!m_map_group)
                    {
                        throw runtime_error("Unable to allocate memory to read the SequentialMapGroup record");
                    }
                    else
                    {
                        SequentialMapGroup* group = (SequentialMapGroup*)&csf12->groups;

                        // startCharCode and endCharCode indicate a range of Unicode characters
                        // startGlyphID is the glyph index for startCharCode
                        // if startCharCode < endCharCode, increment startGlyphID to get the
                        // index for the intervening characters

                        for (uint32 i = 0; i < csf12->numGroups; ++i)
                        {
                            m_map_group[i].startCharCode = read_dword(group[i].startCharCode);
                            m_map_group[i].endCharCode = read_dword(group[i].endCharCode);
                            m_map_group[i].startGlyphID = read_dword(group[i].startGlyphID);
                        }

                        m_map_size = csf12->numGroups;
                    }
                }
            }
        }


    }
    void initialize(const char* facename, bool bold, bool italic)
    {
        load_font(facename, bold, italic);

        read_cmap_table();

        parse_cmap_subtable();
    }
public:
    unicode_to_glyph_index(const char* facename, bool bold, bool italic) : m_hdc(nullptr), m_hfont(nullptr),
        m_error(false), m_error_message(),
        m_cmap_table(nullptr), m_map_group(nullptr),
        m_map_size(0)
    {
        try
        {
            initialize(facename, bold, italic);
        }
        catch (const runtime_error& err)
        {
            m_error = true;

            m_error_message = err.what();
        }
    }
    ~unicode_to_glyph_index()
    {
        if (m_hdc)
        {
            RestoreDC(m_hdc, -1);

            //ReleaseDC(HWND_DESKTOP, m_hdc);
            DeleteDC(m_hdc);
        }
        if (m_hfont)
        {
            DeleteObject(m_hfont);
        }
        if (m_cmap_table)
        {
            delete[] m_cmap_table;
        }
        if (m_map_group)
        {
            delete[] m_map_group;
        }
    }
    bool success() const
    {
        return !m_error;
    }
    string error_message() const
    {
        return m_error_message;
    }
    static int __compare_uchar(const void* a, const void* b)
    {
        const SequentialMapGroup* key = (SequentialMapGroup*)a;
        const SequentialMapGroup* item = (SequentialMapGroup*)b;

        if (key->startCharCode < item->startCharCode)
            return -1;
        else if (key->startCharCode > item->endCharCode)
            return 1;
        else
            return 0;
    }
    uint32 char_get_index(uint32 uchar)
    {
        SequentialMapGroup key = { uchar, 0, 0 }, * item;
        //int __compare_uchar(const void *a, const void *b);

        item = (SequentialMapGroup*)bsearch(&key, m_map_group, m_map_size, sizeof(m_map_group[0]), __compare_uchar);

        if (item)
        {
            return item->startGlyphID + (uchar - item->startCharCode);
        }
        // if not found and uchar is just 16 bits, try GDI
        else if (uchar < 65536)
        {
            wchar_t w = (wchar_t)uchar;
            WORD pgi;

            GetGlyphIndicesW(m_hdc, &w, 1, &pgi, GGI_MARK_NONEXISTING_GLYPHS);

            return pgi;
        }

        return 0;
    }
};
