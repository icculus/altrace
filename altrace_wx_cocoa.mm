/**
 * alTrace; a debugging tool for OpenAL.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include <Cocoa/Cocoa.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"  // ignore OpenGL deprecations.
#include <wx/wx.h>
#pragma clang diagnostic pop

typedef unsigned char uchar;

void cocoaGetGridColors(wxColour *even, wxColor *odd)
{
    @autoreleasepool {
        CGFloat r, g, b, a;
        NSArray<NSColor *> *colors = NSColor.controlAlternatingRowBackgroundColors;
        [[[colors objectAtIndex:0] colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]] getRed:&r green:&g blue:&b alpha:&a];
        *even = wxColour((uchar) (r * 255.0f), (uchar) (g * 255.0f), (uchar) (b * 255.0f), (uchar) (a * 255.0f));
        [[[colors objectAtIndex:1] colorUsingColorSpace:[NSColorSpace deviceRGBColorSpace]] getRed:&r green:&g blue:&b alpha:&a];
        *odd = wxColour((uchar) (r * 255.0f), (uchar) (g * 255.0f), (uchar) (b * 255.0f), (uchar) (a * 255.0f));
    }
}

// end of altrace_wx_cocoa.mm ...

