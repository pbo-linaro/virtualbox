/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include <stdio.h>
#include "guid.h"

const GUID IID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_ID3D9Adapter = { 0xBF6C7B9A, 0xF0BE, 0x11DF, { 0x81, 0xE3, 0x7F, 0x57, 0xDF, 0xD7, 0x20, 0x85 } };
const GUID IID_IDirect3D9ExOverlayExtension = { 0x187AEB13, 0xAAF5, 0x4C59, { 0x87, 0x6D, 0xE0, 0x59, 0x08, 0x8C, 0x0D, 0xF8 } };
const GUID IID_IDirect3DAuthenticatedChannel9 = { 0xFF24BEEE, 0xDA21, 0x4BEB, { 0x98, 0xB5, 0xD2, 0xF8, 0x99, 0xF9, 0x8A, 0xF9 } };
const GUID IID_IDirect3DBaseTexture9 = { 0x580CA87E, 0x1D3C, 0x4D54, { 0x99, 0x1D, 0xB7, 0xD3, 0xE3, 0xC2, 0x98, 0xCE } };
const GUID IID_IDirect3DCryptoSession9 = { 0xFA0AB799, 0x7A9C, 0x48CA, { 0x8C, 0x5B, 0x23, 0x7E, 0x71, 0xA5, 0x44, 0x34 } };
const GUID IID_IDirect3DCubeTexture9 = { 0xFFF32F81, 0xD953, 0x473A, { 0x92, 0x23, 0x93, 0xD6, 0x52, 0xAB, 0xA9, 0x3F } };
const GUID IID_IDirect3DDevice9 = { 0xD0223B96, 0xBF7A, 0x43FD, { 0x92, 0xBD, 0xA4, 0x3B, 0x0D, 0x82, 0xB9, 0xEB } };
const GUID IID_IDirect3DDevice9Ex = { 0xB18B10CE, 0x2649, 0x405A, { 0x87, 0x0F, 0x95, 0xF7, 0x77, 0xD4, 0x31, 0x3A } };
const GUID IID_IDirect3DDevice9Video = { 0x26DC4561, 0xA1EE, 0x4AE7, { 0x96, 0xDA, 0x11, 0x8A, 0x36, 0xC0, 0xEC, 0x95 } };
const GUID IID_IDirect3DIndexBuffer9 = { 0x7C9DD65E, 0xD3F7, 0x4529, { 0xAC, 0xEE, 0x78, 0x58, 0x30, 0xAC, 0xDE, 0x35 } };
const GUID IID_IDirect3DPixelShader9 = { 0x6D3BDBDC, 0x5B02, 0x4415, { 0xB8, 0x52, 0xCE, 0x5E, 0x8B, 0xCC, 0xB2, 0x89 } };
const GUID IID_IDirect3DQuery9 = { 0xD9771460, 0xA695, 0x4F26, { 0xBB, 0xD3, 0x27, 0xB8, 0x40, 0xB5, 0x41, 0xCC } };
const GUID IID_IDirect3DResource9 = { 0x05EEC05D, 0x8F7D, 0x4362, { 0xB9, 0x99, 0xD1, 0xBA, 0xF3, 0x57, 0xC7, 0x4 } };
const GUID IID_IDirect3DStateBlock9 = { 0xB07C4FE5, 0x310D, 0x4BA8, { 0xA2, 0x3C, 0x4F, 0x0F, 0x20, 0x6F, 0x21, 0x8B } };
const GUID IID_IDirect3DSurface9 = { 0x0CFBAF3A, 0x9FF6, 0x429A, { 0x99, 0xB3, 0xA2, 0x79, 0x6A, 0xF8, 0xB8, 0x9B } };
const GUID IID_IDirect3DSwapChain9 = { 0x794950F2, 0xADFC, 0x458A, { 0x90, 0x5E, 0x10, 0xA1, 0x0B, 0x0B, 0x50, 0x3B } };
const GUID IID_IDirect3DSwapChain9Ex = { 0x91886CAF, 0x1C3D, 0x4D2E, { 0xA0, 0xAB, 0x3E, 0x4C, 0x7D, 0x8D, 0x33, 0x3 } };
const GUID IID_IDirect3DTexture9 = { 0x85C31227, 0x3DE5, 0x4F00, { 0x9B, 0x3A, 0xF1, 0x1A, 0xC3, 0x8C, 0x18, 0xB5 } };
const GUID IID_IDirect3DVertexBuffer9 = { 0xB64BB1B5, 0xFD70, 0x4DF6, { 0xBF, 0x91, 0x19, 0xD0, 0xA1, 0x24, 0x55, 0xE3 } };
const GUID IID_IDirect3DVertexDeclaration9 = { 0xDD13C59C, 0x36FA, 0x4098, { 0xA8, 0xFB, 0xC7, 0xED, 0x39, 0xDC, 0x85, 0x46 } };
const GUID IID_IDirect3DVertexShader9 = { 0xEFC5557E, 0x6265, 0x4613, { 0x8A, 0x94, 0x43, 0x85, 0x78, 0x89, 0xEB, 0x36 } };
const GUID IID_IDirect3DVolume9 = { 0x24F416E6, 0x1F67, 0x4AA7, { 0xB8, 0x8E, 0xD3, 0x3F, 0x6F, 0x31, 0x28, 0xA1 } };
const GUID IID_IDirect3DVolumeTexture9 = { 0x2518526C, 0xE789, 0x4111, { 0xA7, 0xB9, 0x47, 0xEF, 0x32, 0x8D, 0x13, 0xE6 } };

boolean
GUID_equal( const GUID *a,
            const GUID *b )
{
    unsigned i;

    if (!a || !b)
        return FALSE;

    if (a->Data1 != b->Data1 ||
        a->Data2 != b->Data2 ||
        a->Data3 != b->Data3) { return FALSE; }
    for (i = 0; i < 8; i++) {
        if (a->Data4[i] != b->Data4[i]) { return FALSE; }
    }
    return TRUE;
}

char* GUID_sprintf(char *guid_str, REFGUID id) {
    snprintf( guid_str, 64,             /* VBox sprintf -> snprintf; all callers are in DBG() and using a 64 byte buffer */
             "{%08X,%04X,%04X,%02X%02X%02X%02X%02X%02X%02X%02X}",
             id->Data1,
             id->Data2,
             id->Data3,
             id->Data4[0],
             id->Data4[1],
             id->Data4[2],
             id->Data4[3],
             id->Data4[4],
             id->Data4[5],
             id->Data4[6],
             id->Data4[7]);
    return guid_str;
}
