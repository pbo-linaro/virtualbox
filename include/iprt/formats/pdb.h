/* $Id$ */
/** @file
 * IPRT - Microsoft Program Database (PDB) Structures and Constants.
 */

/*
 * Copyright (C) 2023 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_formats_pdb_h
#define IPRT_INCLUDED_formats_pdb_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_rt_formats_pdbg     Microsoft Program Database (PDB) structures and definitions.
 * @ingroup grp_rt_formats
 * @{
 */

/** @name PDB 1.0
 *
 * This is just a type info database, rather than an abstract streams container
 * like in version 2.0 and later.
 *
 * @note Seen in EX04.PDB shipping with Visual C++ 1.5.
 * @{ */
/** The PDB 1.00 signature. */
#define RTPDB_SIGNATURE_100 "Microsoft C/C++ program database 1.00\r\n\x1A" "JG\0"

/**
 * The PDB 1.0 header.
 */
typedef struct RTPDB10HDR
{
    /** The signature string. */
    uint8_t         szSignature[sizeof(RTPDB_SIGNATURE_100)];
    /** The compiler version (RTDBG10HDR_VERSION_XXX). */
    uint32_t        uVersion;
    /** PDB timestamp/signature. */
    uint32_t        uTimestamp;
    /** PDB age. */
    uint32_t        uAge;
    /** The index of the first TI record. */
    uint16_t        idxTypeInfoFirst;
    /** The index of the last TI records + 1. */
    uint16_t        idxTypeInfoEnd;
    /** The size of the hashed type info records following this header. */
    uint32_t        cbTypeInfo;
    /* Following are cbTypeInfo bytes worth of hashed type info (RTPDB10HASHEDTI). */
} RTPDB10HDR;
AssertCompileSize(RTPDB10HDR, 64);
typedef RTPDB10HDR *PRTPDB10HDR;
typedef RTPDB10HDR const *PCRTPDB10HDR;

/** Typical RTPDB10HDR::uVersion value. */
#define RTDBG10HDR_VERSION_VC10     UINT32_C(920924) /* 0x000e0d5c */

/** PDB 1.0 hashed type info records. */
typedef struct RTPDB10HASHEDTI
{
    uint16_t        uHash;
    uint8_t         cbInfo;
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint8_t         abData[RT_FLEXIBLE_ARRAY];
} RTPDB10HASHEDTI;
typedef RTPDB10HASHEDTI *PRTPDB10HASHEDTI;
typedef RTPDB10HASHEDTI const *PCRTPDB10HASHEDTI;

/** @} */


/** @name PDB 2.0
 *
 * This was presumably introduced with Visual C++ v2.0.
 *
 * @{ */
/** A PDB 2.0 Page number. */
typedef uint16_t RTPDB20PAGE;
/** Pointer to a PDB 2.0 Page number. */
typedef RTPDB20PAGE *PRTPDB20PAGE;
/** Pointer to a const PDB 2.0 Page number. */
typedef RTPDB20PAGE const *PCRTPDB20PAGE;

/**
 * A PDB 2.0 stream.
 */
typedef struct RTPDB20STREAM
{
    /** The size of the stream. */
    uint32_t        cbStream;
    /** Some unknown value. */
    uint32_t        u32Unknown;
} RTPDB20STREAM;
typedef RTPDB20STREAM *PRTPDB20STREAM;

/** The PDB 2.00 signature. */
#define RTPDB_SIGNATURE_200 "Microsoft C/C++ program database 2.00\r\n\x1A" "JG\0"
/**
 * The PDB 2.0 header.
 */
typedef struct RTPDB20HDR
{
    /** The signature string. */
    uint8_t         szSignature[sizeof(RTPDB_SIGNATURE_200)];
    /** The page size. */
    uint32_t        cbPage;
    /** The start page - whatever that is... */
    RTPDB20PAGE     iStartPage;
    /** The number of pages in the file. */
    RTPDB20PAGE     cPages;
    /** The root stream directory. */
    RTPDB20STREAM   RootStream;
    /** The root page table. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTPDB20PAGE     aiRootPageMap[RT_FLEXIBLE_ARRAY];
} RTPDB20HDR;
AssertCompileMemberOffset(RTPDB20HDR, aiRootPageMap, 60);
typedef RTPDB20HDR *PRTPDB20HDR;
typedef RTPDB20HDR const *PCRTPDB20HDR;

/**
 * The PDB 2.0 root directory.
 */
typedef struct RTPDB20ROOT
{
    /** The number of streams */
    uint16_t        cStreams;
    /** Reserved or high part of cStreams. */
    uint16_t        u16Reserved;
    /** Array of streams. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTPDB20STREAM   aStreams[RT_FLEXIBLE_ARRAY];
} RTPDB20ROOT;
typedef RTPDB20ROOT *PRTPDB20ROOT;
typedef RTPDB20ROOT const *PCRTPDB20ROOT;

/**
 * The PDB 2.0 name stream (#1) header.
 */
typedef struct RTPDB20NAMES
{
    /** The structure version (RTPDB20NAMES_VERSION_XXX). */
    uint32_t        uVersion;
    /** Timestamp or similar to help uniquely identify the PDB.  */
    uint32_t        uTimestamp;
    /** PDB file age. */
    uint32_t        uAge;
    /** The size of the following name table. */
    uint32_t        cbNames;
#if 0
    /** The name table. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char            szzNames[RT_FLEXIBLE_ARRAY];
    /* Followed by ID to string table mappings. */
    uint32_t        cbIdMap;
#endif
} RTPDB20NAMES;
AssertCompileSize(RTPDB20NAMES, 16);
typedef RTPDB20NAMES *PRTPDB20NAMES;
typedef RTPDB20NAMES const *PCRTPDB20NAMES;

/** The version / magic of the names structure for Visual C++ v2.0. */
#define RTPDB20NAMES_VERSION_VC20   UINT32_C(19941610)
/** The version / magic of the names structure for Visual C++ v4.0. */
#define RTPDB20NAMES_VERSION_VC40   UINT32_C(19950623)
/** The version / magic of the names structure for Visual C++ v4.1. */
#define RTPDB20NAMES_VERSION_VC41   UINT32_C(19950814)
/** The version / magic of the names structure for Visual C++ v5.0. */
#define RTPDB20NAMES_VERSION_VC50   UINT32_C(19960307)

/** The version / magic of the names structure for Visual C++ v6.0.
 * @note This is the only one tested.  */
#define RTPDB20NAMES_VERSION_VC60   UINT32_C(19970604)

/** @} */


/** @name PDB 7.0
 * @{ */

/** A PDB 7.0 Page number. */
typedef uint32_t RTPDB70PAGE;
/** Pointer to a PDB 7.0 Page number. */
typedef RTPDB70PAGE *PRTPDB70PAGE;
/** Pointer to a const PDB 7.0 Page number. */
typedef RTPDB70PAGE const *PCRTPDB70PAGE;

/**
 * A PDB 7.0 stream.
 */
typedef struct RTPDB70STREAM
{
    /** The size of the stream. */
    uint32_t        cbStream;
} RTPDB70STREAM;
typedef RTPDB70STREAM *PRTPDB70STREAM;


/** The PDB 7.00 signature. */
#define RTPDB_SIGNATURE_700 "Microsoft C/C++ MSF 7.00\r\n\x1A" "DS\0\0"
/**
 * The PDB 7.0 header.
 */
typedef struct RTPDB70HDR
{
    /** The signature string. */
    uint8_t         szSignature[sizeof(RTPDB_SIGNATURE_700)];
    /** The page size. */
    uint32_t        cbPage;
    /** The start page. */
    RTPDB70PAGE     iStartPage;
    /** The number of pages in the file. */
    RTPDB70PAGE     cPages;
    /** The root stream directory. */
    uint32_t        cbRoot;
    /** Unknown function, always 0. */
    uint32_t        u32Reserved;
    /** The page index of the root page table. */
    RTPDB70PAGE     iRootPages;
} RTPDB70HDR;
AssertCompileSize(RTPDB70HDR, 56);
typedef RTPDB70HDR *PRTPDB70HDR;
typedef RTPDB70HDR const *PCRTPDB70HDR;

/**
 * The PDB 7.0 root directory.
 */
typedef struct RTPDB70ROOT
{
    /** The number of streams */
    uint32_t        cStreams;
    /** Array of streams. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTPDB70STREAM   aStreams[RT_FLEXIBLE_ARRAY];
    /* RTPDB70PAGE aiPages[] */
} RTPDB70ROOT;
AssertCompileMemberOffset(RTPDB70ROOT, aStreams, 4);
typedef RTPDB70ROOT *PRTPDB70ROOT;
typedef RTPDB70ROOT const *PCRTPDB70ROOT;

/**
 * The PDB 7.0 name stream (#1) header.
 */
#pragma pack(4)
typedef struct RTPDB70NAMES
{
    /** The structure version (typically RTPDB70NAMES_VERSION). */
    uint32_t        uVersion;
    /** PDB creation timestamp.  */
    uint32_t        uTimestamp;
    /** PDB age. */
    uint32_t        uAge;
    /** PDB unique identifier. */
    RTUUID          Uuid;

    /** The size of the following name table. */
    uint32_t        cbNames;
#if 0
    /** The name table. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char            szzNames[RT_FLEXIBLE_ARRAY];
    /* Followed by ID to string table mappings. */
    uint32_t        cbIdMap;
    /* ... */
#endif
} RTPDB70NAMES;
#pragma pack()
AssertCompileSize(RTPDB70NAMES, 12 + 16 + 4);
typedef RTPDB70NAMES *PRTPDB70NAMES;
typedef RTPDB70NAMES const *PCRTPDB70NAMES;

/** The version / magic of the names structure.
 * This is for VC++ v7.0. It's technically possible it may have other values,
 * like 19990604 (v7.0 dep), 20030901 (v8.0), 20091201 (v11.0), or 20140508
 * (v14.0), but these haven't been encountered yet. */
#define RTPDB70NAMES_VERSION    UINT32_C(20000404)

/** @} */

/**
 * Helper for converting a stream size to a page count.
 *
 * @returns Number of pages.
 * @param   cbStream    The stream size.
 * @param   cbPage      The page size (must not be zero).
 */
DECLINLINE(uint32_t) RTPdbSizeToPages(uint32_t cbStream, uint32_t cbPage)
{
    if (cbStream == UINT32_MAX || !cbStream)
        return 0;
    return (uint32_t)(((uint64_t)cbStream + cbPage - 1) / cbPage);
}

/** @} */

#endif /* !IPRT_INCLUDED_formats_pdb_h */

