#ifndef G80_TEXTURE_XML
#define G80_TEXTURE_XML

/* Autogenerated file, DO NOT EDIT manually!

This file was generated by the rules-ng-ng headergen tool in this git repository:
https://github.com/envytools/envytools/
git clone https://github.com/envytools/envytools.git

The rules-ng-ng source files this header was generated from are:
- envytools/rnndb/./graph/g80_texture.xml  (  19104 bytes, from 2021-03-09 21:53:05)
- envytools/rnndb/copyright.xml            (   6431 bytes, from 2021-03-01 01:32:28)
- envytools/rnndb-generated/nvchipsets.xml (   3335 bytes, from 2020-04-11 17:28:38)
- envytools/rnndb/g80_defs.xml             (  21781 bytes, from 2021-03-09 21:57:03)
- envytools/rnndb/nv_defs.xml              (   5522 bytes, from 2021-03-01 01:32:28)

Copyright (C) 2006-2021 by the following authors:
- Artur Huillet <arthur.huillet@free.fr> (ahuillet)
- Ben Skeggs (darktama, darktama_)
- B. R. <koala_br@users.sourceforge.net> (koala_br)
- Carlos Martin <carlosmn@users.sf.net> (carlosmn)
- Christoph Bumiller <e0425955@student.tuwien.ac.at> (calim, chrisbmr)
- Dawid Gajownik <gajownik@users.sf.net> (gajownik)
- Dmitry Baryshkov
- Dmitry Eremin-Solenikov <lumag@users.sf.net> (lumag)
- EdB <edb_@users.sf.net> (edb_)
- Erik Waling <erikwailing@users.sf.net> (erikwaling)
- Francisco Jerez <currojerez@riseup.net> (curro)
- Ilia Mirkin <imirkin@alum.mit.edu> (imirkin)
- jb17bsome <jb17bsome@bellsouth.net> (jb17bsome)
- Jeremy Kolb <kjeremy@users.sf.net> (kjeremy)
- Laurent Carlier <lordheavym@gmail.com> (lordheavy)
- Luca Barbieri <luca@luca-barbieri.com> (lb, lb1)
- Maarten Maathuis <madman2003@gmail.com> (stillunknown)
- Marcelina Kościelnicka <mwk@0x04.net> (mwk)
- Mark Carey <mark.carey@gmail.com> (careym)
- Matthieu Castet <matthieu.castet@parrot.com> (mat-c)
- nvidiaman <nvidiaman@users.sf.net> (nvidiaman)
- Patrice Mandin <patmandin@gmail.com> (pmandin, pmdata)
- Pekka Paalanen <pq@iki.fi> (pq, ppaalanen)
- Peter Popov <ironpeter@users.sf.net> (ironpeter)
- Richard Hughes <hughsient@users.sf.net> (hughsient)
- Rudi Cilibrasi <cilibrar@users.sf.net> (cilibrar)
- Serge Martin
- Simon Raffeiner
- Stephane Loeuillet <leroutier@users.sf.net> (leroutier)
- Stephane Marchesin <stephane.marchesin@gmail.com> (marcheu)
- sturmflut <sturmflut@users.sf.net> (sturmflut)
- Sylvain Munaut <tnt@246tNt.com>
- Victor Stinner <victor.stinner@haypocalc.com> (haypo)
- Wladmir van der Laan <laanwj@gmail.com> (miathan6)
- Younes Manton <younes.m@gmail.com> (ymanton)

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial
portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#define G80_TSC_WRAP_WRAP					0x00000000
#define G80_TSC_WRAP_MIRROR					0x00000001
#define G80_TSC_WRAP_CLAMP_TO_EDGE				0x00000002
#define G80_TSC_WRAP_BORDER					0x00000003
#define G80_TSC_WRAP_CLAMP_OGL					0x00000004
#define G80_TSC_WRAP_MIRROR_ONCE_CLAMP_TO_EDGE			0x00000005
#define G80_TSC_WRAP_MIRROR_ONCE_BORDER				0x00000006
#define G80_TSC_WRAP_MIRROR_ONCE_CLAMP_OGL			0x00000007
#define G80_TIC__SIZE						0x00000020
#define G80_TIC_0						0x00000000
#define GK20A_TIC_0_USE_COMPONENT_SIZES_EXTENDED__MASK		0x80000000
#define GK20A_TIC_0_USE_COMPONENT_SIZES_EXTENDED__SHIFT		31
#define GK20A_TIC_0_USE_COMPONENT_SIZES_EXTENDED_NO		0x00000000
#define GK20A_TIC_0_USE_COMPONENT_SIZES_EXTENDED_YES		0x80000000
#define G84_TIC_0_PACK_COMPONENTS				0x40000000
#define G80_TIC_0_W_SOURCE__MASK				0x38000000
#define G80_TIC_0_W_SOURCE__SHIFT				27
#define G80_TIC_0_Z_SOURCE__MASK				0x07000000
#define G80_TIC_0_Z_SOURCE__SHIFT				24
#define G80_TIC_0_Y_SOURCE__MASK				0x00e00000
#define G80_TIC_0_Y_SOURCE__SHIFT				21
#define G80_TIC_0_X_SOURCE__MASK				0x001c0000
#define G80_TIC_0_X_SOURCE__SHIFT				18
#define G80_TIC_0_A_DATA_TYPE__MASK				0x00038000
#define G80_TIC_0_A_DATA_TYPE__SHIFT				15
#define G80_TIC_0_B_DATA_TYPE__MASK				0x00007000
#define G80_TIC_0_B_DATA_TYPE__SHIFT				12
#define G80_TIC_0_G_DATA_TYPE__MASK				0x00000e00
#define G80_TIC_0_G_DATA_TYPE__SHIFT				9
#define G80_TIC_0_R_DATA_TYPE__MASK				0x000001c0
#define G80_TIC_0_R_DATA_TYPE__SHIFT				6
#define G80_TIC_0_COMPONENTS_SIZES__MASK			0x0000003f
#define G80_TIC_0_COMPONENTS_SIZES__SHIFT			0
#define G80_TIC_0_COMPONENTS_SIZES_R32_G32_B32_A32		0x00000001
#define GF100_TIC_0_COMPONENTS_SIZES_R32_G32_B32		0x00000002
#define G80_TIC_0_COMPONENTS_SIZES_R16_G16_B16_A16		0x00000003
#define G80_TIC_0_COMPONENTS_SIZES_R32_G32			0x00000004
#define G80_TIC_0_COMPONENTS_SIZES_R32_B24G8			0x00000005
#define G80_TIC_0_COMPONENTS_SIZES_X8B8G8R8			0x00000007
#define G80_TIC_0_COMPONENTS_SIZES_A8B8G8R8			0x00000008
#define G80_TIC_0_COMPONENTS_SIZES_A2B10G10R10			0x00000009
#define G80_TIC_0_COMPONENTS_SIZES_R16_G16			0x0000000c
#define G80_TIC_0_COMPONENTS_SIZES_G8R24			0x0000000d
#define G80_TIC_0_COMPONENTS_SIZES_G24R8			0x0000000e
#define G80_TIC_0_COMPONENTS_SIZES_R32				0x0000000f
#define G80_TIC_0_COMPONENTS_SIZES_A4B4G4R4			0x00000012
#define G80_TIC_0_COMPONENTS_SIZES_A5B5G5R1			0x00000013
#define G80_TIC_0_COMPONENTS_SIZES_A1B5G5R5			0x00000014
#define G80_TIC_0_COMPONENTS_SIZES_B5G6R5			0x00000015
#define G80_TIC_0_COMPONENTS_SIZES_B6G5R5			0x00000016
#define G80_TIC_0_COMPONENTS_SIZES_G8R8				0x00000018
#define G80_TIC_0_COMPONENTS_SIZES_R16				0x0000001b
#define G80_TIC_0_COMPONENTS_SIZES_Y8_VIDEO			0x0000001c
#define G80_TIC_0_COMPONENTS_SIZES_R8				0x0000001d
#define G80_TIC_0_COMPONENTS_SIZES_G4R4				0x0000001e
#define G80_TIC_0_COMPONENTS_SIZES_R1				0x0000001f
#define G80_TIC_0_COMPONENTS_SIZES_E5B9G9R9_SHAREDEXP		0x00000020
#define G80_TIC_0_COMPONENTS_SIZES_BF10GF11RF11			0x00000021
#define G80_TIC_0_COMPONENTS_SIZES_G8B8G8R8			0x00000022
#define G80_TIC_0_COMPONENTS_SIZES_B8G8R8G8			0x00000023
#define G80_TIC_0_COMPONENTS_SIZES_DXT1				0x00000024
#define G80_TIC_0_COMPONENTS_SIZES_DXT23			0x00000025
#define G80_TIC_0_COMPONENTS_SIZES_DXT45			0x00000026
#define G80_TIC_0_COMPONENTS_SIZES_DXN1				0x00000027
#define G80_TIC_0_COMPONENTS_SIZES_DXN2				0x00000028
#define GF100_TIC_0_COMPONENTS_SIZES_BC6H_SF16			0x00000010
#define GF100_TIC_0_COMPONENTS_SIZES_BC6H_UF16			0x00000011
#define GF100_TIC_0_COMPONENTS_SIZES_BC7U			0x00000017
#define GK20A_TIC_0_COMPONENTS_SIZES_ETC2_RGB			0x00000006
#define GK20A_TIC_0_COMPONENTS_SIZES_ETC2_RGB_PTA		0x0000000a
#define GK20A_TIC_0_COMPONENTS_SIZES_ETC2_RGBA			0x0000000b
#define GK20A_TIC_0_COMPONENTS_SIZES_EAC			0x00000019
#define GK20A_TIC_0_COMPONENTS_SIZES_EACX2			0x0000001a
#define G80_TIC_0_COMPONENTS_SIZES_Z24S8			0x00000029
#define G80_TIC_0_COMPONENTS_SIZES_X8Z24			0x0000002a
#define G80_TIC_0_COMPONENTS_SIZES_S8Z24			0x0000002b
#define G80_TIC_0_COMPONENTS_SIZES_X4V4Z24__COV4R4V		0x0000002c
#define G80_TIC_0_COMPONENTS_SIZES_X4V4Z24__COV8R8V		0x0000002d
#define G80_TIC_0_COMPONENTS_SIZES_V8Z24__COV4R12V		0x0000002e
#define G80_TIC_0_COMPONENTS_SIZES_ZF32				0x0000002f
#define G80_TIC_0_COMPONENTS_SIZES_ZF32_X24S8			0x00000030
#define G80_TIC_0_COMPONENTS_SIZES_X8Z24_X20V4S8__COV4R4V	0x00000031
#define G80_TIC_0_COMPONENTS_SIZES_X8Z24_X20V4S8__COV8R8V	0x00000032
#define G80_TIC_0_COMPONENTS_SIZES_ZF32_X20V4X8__COV4R4V	0x00000033
#define G80_TIC_0_COMPONENTS_SIZES_ZF32_X20V4X8__COV8R8V	0x00000034
#define G80_TIC_0_COMPONENTS_SIZES_ZF32_X20V4S8__COV4R4V	0x00000035
#define G80_TIC_0_COMPONENTS_SIZES_ZF32_X20V4S8__COV8R8V	0x00000036
#define G80_TIC_0_COMPONENTS_SIZES_X8Z24_X16V8S8__COV4R12V	0x00000037
#define G80_TIC_0_COMPONENTS_SIZES_ZF32_X16V8X8__COV4R12V	0x00000038
#define G80_TIC_0_COMPONENTS_SIZES_ZF32_X16V8S8__COV4R12V	0x00000039
#define G200_TIC_0_COMPONENTS_SIZES_Z16				0x0000003a
#define G200_TIC_0_COMPONENTS_SIZES_V8Z24__COV8R24V		0x0000003b
#define G200_TIC_0_COMPONENTS_SIZES_X8Z24_X16V8S8__COV8R24V	0x0000003c
#define G200_TIC_0_COMPONENTS_SIZES_ZF32_X16V8X8__COV8R24V	0x0000003d
#define G200_TIC_0_COMPONENTS_SIZES_ZF32_X16V8S8__COV8R24V	0x0000003e
#define G80_TIC_0_COMPONENTS_SIZES__MASK			0x0000003f
#define G80_TIC_0_COMPONENTS_SIZES__SHIFT			0
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_4X4		0x00000000
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_5X4		0x00000010
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_5X5		0x00000001
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_6X5		0x00000011
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_6X6		0x00000002
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_8X5		0x00000015
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_8X6		0x00000012
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_8X8		0x00000004
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_10X5		0x00000016
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_10X6		0x00000017
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_10X8		0x00000013
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_10X10		0x00000005
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_12X10		0x00000014
#define GK20A_TIC_0_COMPONENTS_SIZES_ASTC_2D_12X12		0x00000006

#define G80_TIC_1						0x00000004
#define G80_TIC_1_OFFSET_LOWER__MASK				0xffffffff
#define G80_TIC_1_OFFSET_LOWER__SHIFT				0

#define G80_TIC_2						0x00000008
#define G80_TIC_2_OFFSET_UPPER__MASK				0x000000ff
#define G80_TIC_2_OFFSET_UPPER__SHIFT				0
#define G84_TIC_2_ANISO_SPREAD_MAX_LOG2_LSB__MASK		0x00000300
#define G84_TIC_2_ANISO_SPREAD_MAX_LOG2_LSB__SHIFT		8
#define G80_TIC_2_SRGB_CONVERSION				0x00000400
#define G84_TIC_2_ANISO_SPREAD_MAX_LOG2_MSB			0x00000800
#define G80_TIC_2_LOD_ANISO_QUALITY_2				0x00001000
#define G80_TIC_2_COLOR_KEY_OP					0x00002000
#define G80_TIC_2_TEXTURE_TYPE__MASK				0x0003c000
#define G80_TIC_2_TEXTURE_TYPE__SHIFT				14
#define G80_TIC_2_TEXTURE_TYPE_ONE_D				0x00000000
#define G80_TIC_2_TEXTURE_TYPE_TWO_D				0x00004000
#define G80_TIC_2_TEXTURE_TYPE_THREE_D				0x00008000
#define G80_TIC_2_TEXTURE_TYPE_CUBEMAP				0x0000c000
#define G80_TIC_2_TEXTURE_TYPE_ONE_D_ARRAY			0x00010000
#define G80_TIC_2_TEXTURE_TYPE_TWO_D_ARRAY			0x00014000
#define G80_TIC_2_TEXTURE_TYPE_ONE_D_BUFFER			0x00018000
#define G80_TIC_2_TEXTURE_TYPE_TWO_D_NO_MIPMAP			0x0001c000
#define G80_TIC_2_TEXTURE_TYPE_CUBE_ARRAY			0x00020000
#define G80_TIC_2_LAYOUT__MASK					0x00040000
#define G80_TIC_2_LAYOUT__SHIFT					18
#define G80_TIC_2_LAYOUT_BLOCKLINEAR				0x00000000
#define G80_TIC_2_LAYOUT_PITCH					0x00040000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH__MASK			0x00380000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH__SHIFT			19
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH__MIN			0x00000000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH__MAX			0x00000000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH_ONE			0x00000000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH_TWO			0x00080000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH_FOUR			0x00100000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH_EIGHT			0x00180000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH_SIXTEEN			0x00200000
#define G80_TIC_2_GOBS_PER_BLOCK_WIDTH_THIRTYTWO		0x00280000
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT__MASK			0x01c00000
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT__SHIFT			22
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT_ONE			0x00000000
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT_TWO			0x00400000
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT_FOUR			0x00800000
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT_EIGHT			0x00c00000
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT_SIXTEEN			0x01000000
#define G80_TIC_2_GOBS_PER_BLOCK_HEIGHT_THIRTYTWO		0x01400000
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH__MASK			0x0e000000
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH__SHIFT			25
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH_ONE			0x00000000
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH_TWO			0x02000000
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH_FOUR			0x04000000
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH_EIGHT			0x06000000
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH_SIXTEEN			0x08000000
#define G80_TIC_2_GOBS_PER_BLOCK_DEPTH_THIRTYTWO		0x0a000000
#define G80_TIC_2_SECTOR_PROMOTION__MASK			0x30000000
#define G80_TIC_2_SECTOR_PROMOTION__SHIFT			28
#define G80_TIC_2_SECTOR_PROMOTION_NO_PROMOTION			0x00000000
#define G80_TIC_2_SECTOR_PROMOTION_PROMOTE_TO_2_V		0x10000000
#define G80_TIC_2_SECTOR_PROMOTION_PROMOTE_TO_2_H		0x20000000
#define G80_TIC_2_SECTOR_PROMOTION_PROMOTE_TO_4			0x30000000
#define G80_TIC_2_BORDER_SOURCE__MASK				0x40000000
#define G80_TIC_2_BORDER_SOURCE__SHIFT				30
#define G80_TIC_2_BORDER_SOURCE_TEXTURE				0x00000000
#define G80_TIC_2_BORDER_SOURCE_COLOR				0x40000000
#define G80_TIC_2_NORMALIZED_COORDS				0x80000000

#define G80_TIC_3						0x0000000c
#define G80_TIC_3_PITCH__MASK					0x000fffff
#define G80_TIC_3_PITCH__SHIFT					0
#define G80_TIC_3_LOD_ANISO_QUALITY__MASK			0x00100000
#define G80_TIC_3_LOD_ANISO_QUALITY__SHIFT			20
#define G80_TIC_3_LOD_ANISO_QUALITY_LOW				0x00000000
#define G80_TIC_3_LOD_ANISO_QUALITY_HIGH			0x00100000
#define G80_TIC_3_LOD_ISO_QUALITY__MASK				0x00200000
#define G80_TIC_3_LOD_ISO_QUALITY__SHIFT			21
#define G80_TIC_3_LOD_ISO_QUALITY_LOW				0x00000000
#define G80_TIC_3_LOD_ISO_QUALITY_HIGH				0x00200000
#define G80_TIC_3_ANISO_COARSE_SPREAD_MODIFIER__MASK		0x00c00000
#define G80_TIC_3_ANISO_COARSE_SPREAD_MODIFIER__SHIFT		22
#define G80_TIC_3_ANISO_COARSE_SPREAD_MODIFIER_NONE		0x00000000
#define G80_TIC_3_ANISO_COARSE_SPREAD_MODIFIER_CONST_ONE	0x00400000
#define G80_TIC_3_ANISO_COARSE_SPREAD_MODIFIER_CONST_TWO	0x00800000
#define G80_TIC_3_ANISO_COARSE_SPREAD_MODIFIER_SQRT		0x00c00000
#define G80_TIC_3_ANISO_SPREAD_SCALE__MASK			0x1f000000
#define G80_TIC_3_ANISO_SPREAD_SCALE__SHIFT			24
#define G80_TIC_3_USE_HEADER_OPT_CONTROL			0x20000000
#define G84_TIC_3_ANISO_CLAMP_AT_MAX_LOD			0x40000000
#define G84_TIC_3_ANISO_POW2					0x80000000

#define G80_TIC_4						0x00000010
#define G80_TIC_4_WIDTH__MASK					0x3fffffff
#define G80_TIC_4_WIDTH__SHIFT					0
#define G80_TIC_4_DEPTH_TEXTURE					0x40000000
#define G84_TIC_4_USE_TEXTURE_HEADER_V2				0x80000000

#define G80_TIC_5						0x00000014
#define G80_TIC_5_MAP_MIP_LEVEL__MASK				0xf0000000
#define G80_TIC_5_MAP_MIP_LEVEL__SHIFT				28
#define G80_TIC_5_DEPTH__MASK					0x0fff0000
#define G80_TIC_5_DEPTH__SHIFT					16
#define G80_TIC_5_HEIGHT__MASK					0x0000ffff
#define G80_TIC_5_HEIGHT__SHIFT					0

#define G80_TIC_6						0x00000018
#define G80_TIC_6_TRILIN_OPT__MASK				0x0000001f
#define G80_TIC_6_TRILIN_OPT__SHIFT				0
#define G80_TIC_6_MIP_LOD_BIAS__MASK				0x0003ffe0
#define G80_TIC_6_MIP_LOD_BIAS__SHIFT				5
#define G80_TIC_6_MIP_LOD_BIAS__RADIX				0x00000008
#define G80_TIC_6_ANISO_BIAS__MASK				0x00780000
#define G80_TIC_6_ANISO_BIAS__SHIFT				19
#define G80_TIC_6_ANISO_BIAS__RADIX				0x00000004
#define G80_TIC_6_ANISO_FINE_SPREAD_FUNC__MASK			0x01800000
#define G80_TIC_6_ANISO_FINE_SPREAD_FUNC__SHIFT			23
#define G80_TIC_6_ANISO_FINE_SPREAD_FUNC_HALF			0x00000000
#define G80_TIC_6_ANISO_FINE_SPREAD_FUNC_ONE			0x00800000
#define G80_TIC_6_ANISO_FINE_SPREAD_FUNC_TWO			0x01000000
#define G80_TIC_6_ANISO_FINE_SPREAD_FUNC_MAX			0x01800000
#define G80_TIC_6_ANISO_COARSE_SPREAD_FUNC__MASK		0x06000000
#define G80_TIC_6_ANISO_COARSE_SPREAD_FUNC__SHIFT		25
#define G80_TIC_6_ANISO_COARSE_SPREAD_FUNC_HALF			0x00000000
#define G80_TIC_6_ANISO_COARSE_SPREAD_FUNC_ONE			0x02000000
#define G80_TIC_6_ANISO_COARSE_SPREAD_FUNC_TWO			0x04000000
#define G80_TIC_6_ANISO_COARSE_SPREAD_FUNC_MAX			0x06000000
#define G80_TIC_6_MAX_ANISOTROPY__MASK				0x38000000
#define G80_TIC_6_MAX_ANISOTROPY__SHIFT				27
#define G80_TIC_6_MAX_ANISOTROPY_1_TO_1				0x00000000
#define G80_TIC_6_MAX_ANISOTROPY_2_TO_1				0x08000000
#define G80_TIC_6_MAX_ANISOTROPY_4_TO_1				0x10000000
#define G80_TIC_6_MAX_ANISOTROPY_6_TO_1				0x18000000
#define G80_TIC_6_MAX_ANISOTROPY_8_TO_1				0x20000000
#define G80_TIC_6_MAX_ANISOTROPY_10_TO_1			0x28000000
#define G80_TIC_6_MAX_ANISOTROPY_12_TO_1			0x30000000
#define G80_TIC_6_MAX_ANISOTROPY_16_TO_1			0x38000000
#define G80_TIC_6_ANISO_FINE_SPREAD_MODIFIER__MASK		0xc0000000
#define G80_TIC_6_ANISO_FINE_SPREAD_MODIFIER__SHIFT		30
#define G80_TIC_6_ANISO_FINE_SPREAD_MODIFIER_NONE		0x00000000
#define G80_TIC_6_ANISO_FINE_SPREAD_MODIFIER_CONST_ONE		0x40000000
#define G80_TIC_6_ANISO_FINE_SPREAD_MODIFIER_CONST_TWO		0x80000000
#define G80_TIC_6_ANISO_FINE_SPREAD_MODIFIER_SQRT		0xc0000000

#define G80_TIC_7						0x0000001c
#define G80_TIC_7_COLOR_KEY_VALUE__MASK				0xffffffff
#define G80_TIC_7_COLOR_KEY_VALUE__SHIFT			0

#define G84_TIC_7						0x0000001c
#define G84_TIC_7_RES_VIEW_MIN_MIP_LEVEL__MASK			0x0000000f
#define G84_TIC_7_RES_VIEW_MIN_MIP_LEVEL__SHIFT			0
#define G84_TIC_7_RES_VIEW_MAX_MIP_LEVEL__MASK			0x000000f0
#define G84_TIC_7_RES_VIEW_MAX_MIP_LEVEL__SHIFT			4
#define G84_TIC_7_HEIGHT_MSB					0x00000100
#define G84_TIC_7_MULTI_SAMPLE_COUNT__MASK			0x0000f000
#define G84_TIC_7_MULTI_SAMPLE_COUNT__SHIFT			12
#define G84_TIC_7_MULTI_SAMPLE_COUNT_1X1			0x00000000
#define G84_TIC_7_MULTI_SAMPLE_COUNT_2X1			0x00001000
#define G84_TIC_7_MULTI_SAMPLE_COUNT_2X2			0x00002000
#define G84_TIC_7_MULTI_SAMPLE_COUNT_4X2			0x00003000
#define GT215_TIC_7_MULTI_SAMPLE_COUNT_4X2_D3D			0x00004000
#define GT215_TIC_7_MULTI_SAMPLE_COUNT_2X1_D3D			0x00005000
#define GF100_TIC_7_MULTI_SAMPLE_COUNT_4X4			0x00006000
#define G84_TIC_7_MULTI_SAMPLE_COUNT_2X2_VC_4			0x00008000
#define G84_TIC_7_MULTI_SAMPLE_COUNT_2X2_VC_12			0x00009000
#define G84_TIC_7_MULTI_SAMPLE_COUNT_4X2_VC_8			0x0000a000
#define GF100_TIC_7_MULTI_SAMPLE_COUNT_4X2_VC_24		0x0000b000
#define G84_TIC_7_MIN_LOD_CLAMP__MASK				0x0fff0000
#define G84_TIC_7_MIN_LOD_CLAMP__SHIFT				16
#define G84_TIC_7_MIN_LOD_CLAMP__RADIX				0x00000008
#define G84_TIC_7_DEPTH_MSB__MASK				0x70000000
#define G84_TIC_7_DEPTH_MSB__SHIFT				28

#define G80_TSC__SIZE						0x00000020
#define G80_TSC_0						0x00000000
#define G80_TSC_0_ADDRESS_U__MASK				0x00000007
#define G80_TSC_0_ADDRESS_U__SHIFT				0
#define G80_TSC_0_ADDRESS_V__MASK				0x00000038
#define G80_TSC_0_ADDRESS_V__SHIFT				3
#define G80_TSC_0_ADDRESS_P__MASK				0x000001c0
#define G80_TSC_0_ADDRESS_P__SHIFT				6
#define G80_TSC_0_DEPTH_COMPARE					0x00000200
#define G80_TSC_0_DEPTH_COMPARE_FUNC__MASK			0x00001c00
#define G80_TSC_0_DEPTH_COMPARE_FUNC__SHIFT			10
#define G80_TSC_0_DEPTH_COMPARE_FUNC_NEVER			0x00000000
#define G80_TSC_0_DEPTH_COMPARE_FUNC_LESS			0x00000400
#define G80_TSC_0_DEPTH_COMPARE_FUNC_EQUAL			0x00000800
#define G80_TSC_0_DEPTH_COMPARE_FUNC_LEQUAL			0x00000c00
#define G80_TSC_0_DEPTH_COMPARE_FUNC_GREATER			0x00001000
#define G80_TSC_0_DEPTH_COMPARE_FUNC_NOTEQUAL			0x00001400
#define G80_TSC_0_DEPTH_COMPARE_FUNC_GEQUAL			0x00001800
#define G80_TSC_0_DEPTH_COMPARE_FUNC_ALWAYS			0x00001c00
#define G80_TSC_0_SRGB_CONVERSION				0x00002000
#define G80_TSC_0_FONT_FILTER_WIDTH__MASK			0x0001c000
#define G80_TSC_0_FONT_FILTER_WIDTH__SHIFT			14
#define G80_TSC_0_FONT_FILTER_HEIGHT__MASK			0x000e0000
#define G80_TSC_0_FONT_FILTER_HEIGHT__SHIFT			17
#define G80_TSC_0_MAX_ANISOTROPY__MASK				0x00700000
#define G80_TSC_0_MAX_ANISOTROPY__SHIFT				20
#define G80_TSC_0_MAX_ANISOTROPY_1_TO_1				0x00000000
#define G80_TSC_0_MAX_ANISOTROPY_2_TO_1				0x00100000
#define G80_TSC_0_MAX_ANISOTROPY_4_TO_1				0x00200000
#define G80_TSC_0_MAX_ANISOTROPY_6_TO_1				0x00300000
#define G80_TSC_0_MAX_ANISOTROPY_8_TO_1				0x00400000
#define G80_TSC_0_MAX_ANISOTROPY_10_TO_1			0x00500000
#define G80_TSC_0_MAX_ANISOTROPY_12_TO_1			0x00600000
#define G80_TSC_0_MAX_ANISOTROPY_16_TO_1			0x00700000

#define G80_TSC_1						0x00000004
#define G80_TSC_1_MAG_FILTER__MASK				0x00000003
#define G80_TSC_1_MAG_FILTER__SHIFT				0
#define G80_TSC_1_MAG_FILTER_NEAREST				0x00000001
#define G80_TSC_1_MAG_FILTER_LINEAR				0x00000002
#define G80_TSC_1_MIN_FILTER__MASK				0x00000030
#define G80_TSC_1_MIN_FILTER__SHIFT				4
#define G80_TSC_1_MIN_FILTER_NEAREST				0x00000010
#define G80_TSC_1_MIN_FILTER_LINEAR				0x00000020
#define G80_TSC_1_MIP_FILTER__MASK				0x000000c0
#define G80_TSC_1_MIP_FILTER__SHIFT				6
#define G80_TSC_1_MIP_FILTER_NONE				0x00000040
#define G80_TSC_1_MIP_FILTER_NEAREST				0x00000080
#define G80_TSC_1_MIP_FILTER_LINEAR				0x000000c0
#define GK104_TSC_1_CUBEMAP_INTERFACE_FILTERING			0x00000200
#define GM204_TSC_1_REDUCTION_MODE__MASK			0x00000c00
#define GM204_TSC_1_REDUCTION_MODE__SHIFT			10
#define GM204_TSC_1_REDUCTION_MODE_WEIGHTED_AVERAGE		0x00000000
#define GM204_TSC_1_REDUCTION_MODE_MIN				0x00000400
#define GM204_TSC_1_REDUCTION_MODE_MAX				0x00000800
#define G80_TSC_1_MIP_LOD_BIAS__MASK				0x01fff000
#define G80_TSC_1_MIP_LOD_BIAS__SHIFT				12
#define G80_TSC_1_MIP_LOD_BIAS__RADIX				0x00000008
#define GK104_TSC_1_FLOAT_COORD_NORMALIZATION__MASK		0x02000000
#define GK104_TSC_1_FLOAT_COORD_NORMALIZATION__SHIFT		25
#define GK104_TSC_1_FLOAT_COORD_NORMALIZATION_USE_HEADER_SETTING	0x00000000
#define GK104_TSC_1_FLOAT_COORD_NORMALIZATION_FORCE_UNNORMALIZED_COORDS	0x02000000
#define G80_TSC_1_TRILIN_OPT__MASK				0x7c000000
#define G80_TSC_1_TRILIN_OPT__SHIFT				26

#define G80_TSC_2						0x00000008
#define G80_TSC_2_MIN_LOD_CLAMP__MASK				0x00000fff
#define G80_TSC_2_MIN_LOD_CLAMP__SHIFT				0
#define G80_TSC_2_MIN_LOD_CLAMP__RADIX				0x00000008
#define G80_TSC_2_MAX_LOD_CLAMP__MASK				0x00fff000
#define G80_TSC_2_MAX_LOD_CLAMP__SHIFT				12
#define G80_TSC_2_MAX_LOD_CLAMP__RADIX				0x00000008
#define G80_TSC_2_SRGB_BORDER_COLOR_R__MASK			0xff000000
#define G80_TSC_2_SRGB_BORDER_COLOR_R__SHIFT			24

#define G80_TSC_3						0x0000000c
#define G80_TSC_3_SRGB_BORDER_COLOR_G__MASK			0x000ff000
#define G80_TSC_3_SRGB_BORDER_COLOR_G__SHIFT			12
#define G80_TSC_3_SRGB_BORDER_COLOR_B__MASK			0x0ff00000
#define G80_TSC_3_SRGB_BORDER_COLOR_B__SHIFT			20

#define G80_TSC_4						0x00000010
#define G80_TSC_4_BORDER_COLOR_R__MASK				0xffffffff
#define G80_TSC_4_BORDER_COLOR_R__SHIFT				0

#define G80_TSC_5						0x00000014
#define G80_TSC_5_BORDER_COLOR_G__MASK				0xffffffff
#define G80_TSC_5_BORDER_COLOR_G__SHIFT				0

#define G80_TSC_6						0x00000018
#define G80_TSC_6_BORDER_COLOR_B__MASK				0xffffffff
#define G80_TSC_6_BORDER_COLOR_B__SHIFT				0

#define G80_TSC_7						0x0000001c
#define G80_TSC_7_BORDER_COLOR_A__MASK				0xffffffff
#define G80_TSC_7_BORDER_COLOR_A__SHIFT				0


#endif /* G80_TEXTURE_XML */
