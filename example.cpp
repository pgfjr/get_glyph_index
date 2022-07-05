// get_glyph_index.cpp : This file contains the 'main' function. Program execution begins and ends there.
#include "get_glyph_index.hpp"


int main()
{
    unicode_to_glyph_index u("Times New Roman", false, false);

    if (u.success())
    {
        unsigned v = u.char_get_index(0x2211); // summation operator

        cout << v << endl;
    }
    else
    {
        cout << u.error_message();
    }
}

