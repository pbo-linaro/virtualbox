/* $Id$ */
/** @file
 * VBoxManage - The 'internalcommands' command.
 *
 * VBoxInternalManage used to be a second CLI for doing special tricks,
 * not intended for general usage, only for assisting VBox developers.
 * It is now integrated into VBoxManage.
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */



/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>

#include <VBox/VBoxHDD.h>
#include <VBox/sup.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/sha.h>

#include "VBoxManage.h"

/* Includes for the raw disk stuff. */
#ifdef RT_OS_WINDOWS
# include <windows.h>
# include <winioctl.h>
#elif defined(RT_OS_LINUX) || defined(RT_OS_DARWIN) \
    || defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
# include <errno.h>
# include <sys/ioctl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
#endif
#ifdef RT_OS_LINUX
# include <sys/utsname.h>
# include <linux/hdreg.h>
# include <linux/fs.h>
# include <stdlib.h> /* atoi() */
#endif /* RT_OS_LINUX */
#ifdef RT_OS_DARWIN
# include <sys/disk.h>
#endif /* RT_OS_DARWIN */
#ifdef RT_OS_SOLARIS
# include <stropts.h>
# include <sys/dkio.h>
# include <sys/vtoc.h>
#endif /* RT_OS_SOLARIS */
#ifdef RT_OS_FREEBSD
# include <sys/disk.h>
#endif /* RT_OS_FREEBSD */

using namespace com;


/** Macro for checking whether a partition is of extended type or not. */
#define PARTTYPE_IS_EXTENDED(x) ((x) == 0x05 || (x) == 0x0f || (x) == 0x85)

/** Maximum number of partitions we can deal with.
 * Ridiculously large number, but the memory consumption is rather low so who
 * cares about never using most entries. */
#define HOSTPARTITION_MAX 100


typedef struct HOSTPARTITION
{
    unsigned        uIndex;
    /** partition type */
    unsigned        uType;
    /** CHS/cylinder of the first sector */
    unsigned        uStartCylinder;
    /** CHS/head of the first sector */
    unsigned        uStartHead;
    /** CHS/head of the first sector */
    unsigned        uStartSector;
    /** CHS/cylinder of the last sector */
    unsigned        uEndCylinder;
    /** CHS/head of the last sector */
    unsigned        uEndHead;
    /** CHS/sector of the last sector */
    unsigned        uEndSector;
    /** start sector of this partition relative to the beginning of the hard
     * disk or relative to the beginning of the extended partition table */
    uint64_t        uStart;
    /** numer of sectors of the partition */
    uint64_t        uSize;
    /** start sector of this partition _table_ */
    uint64_t        uPartDataStart;
    /** numer of sectors of this partition _table_ */
    uint64_t        cPartDataSectors;
} HOSTPARTITION, *PHOSTPARTITION;

typedef struct HOSTPARTITIONS
{
    unsigned        cPartitions;
    HOSTPARTITION   aPartitions[HOSTPARTITION_MAX];
} HOSTPARTITIONS, *PHOSTPARTITIONS;

/** flag whether we're in internal mode */
bool g_fInternalMode;

/**
 * Print the usage info.
 */
void printUsageInternal(USAGECATEGORY u64Cmd, PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm,
        "Usage: VBoxManage internalcommands <command> [command arguments]\n"
        "\n"
        "Commands:\n"
        "\n"
        "%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
        "WARNING: This is a development tool and shall only be used to analyse\n"
        "         problems. It is completely unsupported and will change in\n"
        "         incompatible ways without warning.\n",

        (u64Cmd & USAGE_LOADSYMS)
        ? "  loadsyms <vmname>|<uuid> <symfile> [delta] [module] [module address]\n"
          "      This will instruct DBGF to load the given symbolfile\n"
          "      during initialization.\n"
          "\n"
        : "",
        (u64Cmd & USAGE_UNLOADSYMS)
        ? "  unloadsyms <vmname>|<uuid> <symfile>\n"
          "      Removes <symfile> from the list of symbol files that\n"
          "      should be loaded during DBF initialization.\n"
          "\n"
        : "",
        (u64Cmd & USAGE_SETHDUUID)
        ? "  sethduuid <filepath> [<uuid>]\n"
          "       Assigns a new UUID to the given image file. This way, multiple copies\n"
          "       of a container can be registered.\n"
          "\n"
        : "",
        (u64Cmd & USAGE_SETHDPARENTUUID)
        ? "  sethdparentuuid <filepath> <uuid>\n"
          "       Assigns a new parent UUID to the given image file.\n"
          "\n"
        : "",
        (u64Cmd & USAGE_DUMPHDINFO)
        ?  "  dumphdinfo <filepath>\n"
           "       Prints information about the image at the given location.\n"
           "\n"
        : "",
        (u64Cmd & USAGE_LISTPARTITIONS)
        ? "  listpartitions -rawdisk <diskname>\n"
          "       Lists all partitions on <diskname>.\n"
          "\n"
        : "",
        (u64Cmd & USAGE_CREATERAWVMDK)
        ? "  createrawvmdk -filename <filename> -rawdisk <diskname>\n"
          "                [-partitions <list of partition numbers> [-mbr <filename>] ]\n"
          "                [-register] [-relative]\n"
          "       Creates a new VMDK image which gives access to an entite host disk (if\n"
          "       the parameter -partitions is not specified) or some partitions of a\n"
          "       host disk. If access to individual partitions is granted, then the\n"
          "       parameter -mbr can be used to specify an alternative MBR to be used\n"
          "       (the partitioning information in the MBR file is ignored).\n"
          "       The diskname is on Linux e.g. /dev/sda, and on Windows e.g.\n"
          "       \\\\.\\PhysicalDrive0).\n"
          "       On Linux host the parameter -relative causes a VMDK file to be created\n"
          "       which refers to individual partitions instead to the entire disk.\n"
          "       Optionally the created image can be immediately registered.\n"
          "       The necessary partition numbers can be queried with\n"
          "         VBoxManage internalcommands listpartitions\n"
          "\n"
        : "",
        (u64Cmd & USAGE_RENAMEVMDK)
        ? "  renamevmdk -from <filename> -to <filename>\n"
          "       Renames an existing VMDK image, including the base file and all its extents.\n"
          "\n"
        : "",
        (u64Cmd & USAGE_CONVERTTORAW)
        ? "  converttoraw [-format <fileformat>] <filename> <outputfile>"
#ifdef ENABLE_CONVERT_RAW_TO_STDOUT
          "|stdout"
#endif /* ENABLE_CONVERT_RAW_TO_STDOUT */
          "\n"
          "       Convert image to raw, writing to file"
#ifdef ENABLE_CONVERT_RAW_TO_STDOUT
          " or stdout"
#endif /* ENABLE_CONVERT_RAW_TO_STDOUT */
          ".\n"
          "\n"
        : "",
        (u64Cmd & USAGE_CONVERTHD)
        ? "  converthd [-srcformat VDI|VMDK|VHD|RAW]\n"
          "            [-dstformat VDI|VMDK|VHD|RAW]\n"
          "            <inputfile> <outputfile>\n"
          "       converts hard disk images between formats\n"
          "\n"
        : "",
#ifdef RT_OS_WINDOWS
        (u64Cmd & USAGE_MODINSTALL)
        ? "  modinstall\n"
          "       Installs the neccessary driver for the host OS\n"
          "\n"
        : "",
        (u64Cmd & USAGE_MODUNINSTALL)
        ? "  moduninstall\n"
          "       Deinstalls the driver\n"
          "\n"
        : "",
#else
        "",
        "",
#endif
        (u64Cmd & USAGE_DEBUGLOG)
        ? "  debuglog <vmname>|<uuid> [--enable|--disable] [--flags todo]\n"
          "           [--groups todo] [--destinations todo]\n"
          "       Controls debug logging.\n"
          "\n"
        : "",
        (u64Cmd & USAGE_PASSWORDHASH)
        ? "  passwordhash <passsword>\n"
          "       Generates a password hash.\n"
          "\n"
        :
          ""
        );
}

/** @todo this is no longer necessary, we can enumerate extra data */
/**
 * Finds a new unique key name.
 *
 * I don't think this is 100% race condition proof, but we assumes
 * the user is not trying to push this point.
 *
 * @returns Result from the insert.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The base key.
 * @param   rKey            Reference to the string object in which we will return the key.
 */
static HRESULT NewUniqueKey(ComPtr<IMachine> pMachine, const char *pszKeyBase, Utf8Str &rKey)
{
    Bstr KeyBase(pszKeyBase);
    Bstr Keys;
    HRESULT hrc = pMachine->GetExtraData(KeyBase.raw(), Keys.asOutParam());
    if (FAILED(hrc))
        return hrc;

    /* if there are no keys, it's simple. */
    if (Keys.isEmpty())
    {
        rKey = "1";
        return pMachine->SetExtraData(KeyBase.raw(), Bstr(rKey).raw());
    }

    /* find a unique number - brute force rulez. */
    Utf8Str KeysUtf8(Keys);
    const char *pszKeys = RTStrStripL(KeysUtf8.c_str());
    for (unsigned i = 1; i < 1000000; i++)
    {
        char szKey[32];
        size_t cchKey = RTStrPrintf(szKey, sizeof(szKey), "%#x", i);
        const char *psz = strstr(pszKeys, szKey);
        while (psz)
        {
            if (    (   psz == pszKeys
                     || psz[-1] == ' ')
                &&  (   psz[cchKey] == ' '
                     || !psz[cchKey])
               )
                break;
            psz = strstr(psz + cchKey, szKey);
        }
        if (!psz)
        {
            rKey = szKey;
            Utf8StrFmt NewKeysUtf8("%s %s", pszKeys, szKey);
            return pMachine->SetExtraData(KeyBase.raw(),
                                          Bstr(NewKeysUtf8).raw());
        }
    }
    RTMsgError("Cannot find unique key for '%s'!", pszKeyBase);
    return E_FAIL;
}


#if 0
/**
 * Remove a key.
 *
 * I don't think this isn't 100% race condition proof, but we assumes
 * the user is not trying to push this point.
 *
 * @returns Result from the insert.
 * @param   pMachine    The machine object.
 * @param   pszKeyBase  The base key.
 * @param   pszKey      The key to remove.
 */
static HRESULT RemoveKey(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey)
{
    Bstr Keys;
    HRESULT hrc = pMachine->GetExtraData(Bstr(pszKeyBase), Keys.asOutParam());
    if (FAILED(hrc))
        return hrc;

    /* if there are no keys, it's simple. */
    if (Keys.isEmpty())
        return S_OK;

    char *pszKeys;
    int rc = RTUtf16ToUtf8(Keys.raw(), &pszKeys);
    if (RT_SUCCESS(rc))
    {
        /* locate it */
        size_t cchKey = strlen(pszKey);
        char *psz = strstr(pszKeys, pszKey);
        while (psz)
        {
            if (    (   psz == pszKeys
                     || psz[-1] == ' ')
                &&  (   psz[cchKey] == ' '
                     || !psz[cchKey])
               )
                break;
            psz = strstr(psz + cchKey, pszKey);
        }
        if (psz)
        {
            /* remove it */
            char *pszNext = RTStrStripL(psz + cchKey);
            if (*pszNext)
                memmove(psz, pszNext, strlen(pszNext) + 1);
            else
                *psz = '\0';
            psz = RTStrStrip(pszKeys);

            /* update */
            hrc = pMachine->SetExtraData(Bstr(pszKeyBase), Bstr(psz));
        }

        RTStrFree(pszKeys);
        return hrc;
    }
    else
        RTMsgError("Failed to delete key '%s' from '%s',  string conversion error %Rrc!",
                   pszKey,  pszKeyBase, rc);

    return E_FAIL;
}
#endif


/**
 * Sets a key value, does necessary error bitching.
 *
 * @returns COM status code.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The key base.
 * @param   pszKey          The key.
 * @param   pszAttribute    The attribute name.
 * @param   pszValue        The string value.
 */
static HRESULT SetString(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey, const char *pszAttribute, const char *pszValue)
{
    HRESULT hrc = pMachine->SetExtraData(BstrFmt("%s/%s/%s", pszKeyBase,
                                                 pszKey, pszAttribute).raw(),
                                         Bstr(pszValue).raw());
    if (FAILED(hrc))
        RTMsgError("Failed to set '%s/%s/%s' to '%s'! hrc=%#x",
                   pszKeyBase, pszKey, pszAttribute, pszValue, hrc);
    return hrc;
}


/**
 * Sets a key value, does necessary error bitching.
 *
 * @returns COM status code.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The key base.
 * @param   pszKey          The key.
 * @param   pszAttribute    The attribute name.
 * @param   u64Value        The value.
 */
static HRESULT SetUInt64(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey, const char *pszAttribute, uint64_t u64Value)
{
    char szValue[64];
    RTStrPrintf(szValue, sizeof(szValue), "%#RX64", u64Value);
    return SetString(pMachine, pszKeyBase, pszKey, pszAttribute, szValue);
}


/**
 * Sets a key value, does necessary error bitching.
 *
 * @returns COM status code.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The key base.
 * @param   pszKey          The key.
 * @param   pszAttribute    The attribute name.
 * @param   i64Value        The value.
 */
static HRESULT SetInt64(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey, const char *pszAttribute, int64_t i64Value)
{
    char szValue[64];
    RTStrPrintf(szValue, sizeof(szValue), "%RI64", i64Value);
    return SetString(pMachine, pszKeyBase, pszKey, pszAttribute, szValue);
}


/**
 * Identical to the 'loadsyms' command.
 */
static int CmdLoadSyms(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    HRESULT rc;

    /*
     * Get the VM
     */
    ComPtr<IMachine> machine;
    CHECK_ERROR_RET(aVirtualBox, FindMachine(Bstr(argv[0]).raw(),
                                             machine.asOutParam()), 1);

    /*
     * Parse the command.
     */
    const char *pszFilename;
    int64_t     offDelta = 0;
    const char *pszModule = NULL;
    uint64_t    ModuleAddress = ~0;
    uint64_t    ModuleSize = 0;

    /* filename */
    if (argc < 2)
        return errorArgument("Missing the filename argument!\n");
    pszFilename = argv[1];

    /* offDelta */
    if (argc >= 3)
    {
        int irc = RTStrToInt64Ex(argv[2], NULL, 0, &offDelta);
        if (RT_FAILURE(irc))
            return errorArgument(argv[0], "Failed to read delta '%s', rc=%Rrc\n", argv[2], rc);
    }

    /* pszModule */
    if (argc >= 4)
        pszModule = argv[3];

    /* ModuleAddress */
    if (argc >= 5)
    {
        int irc = RTStrToUInt64Ex(argv[4], NULL, 0, &ModuleAddress);
        if (RT_FAILURE(irc))
            return errorArgument(argv[0], "Failed to read module address '%s', rc=%Rrc\n", argv[4], rc);
    }

    /* ModuleSize */
    if (argc >= 6)
    {
        int irc = RTStrToUInt64Ex(argv[5], NULL, 0, &ModuleSize);
        if (RT_FAILURE(irc))
            return errorArgument(argv[0], "Failed to read module size '%s', rc=%Rrc\n", argv[5], rc);
    }

    /*
     * Add extra data.
     */
    Utf8Str KeyStr;
    HRESULT hrc = NewUniqueKey(machine, "VBoxInternal/DBGF/loadsyms", KeyStr);
    if (SUCCEEDED(hrc))
        hrc = SetString(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "Filename", pszFilename);
    if (SUCCEEDED(hrc) && argc >= 3)
        hrc = SetInt64(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "Delta", offDelta);
    if (SUCCEEDED(hrc) && argc >= 4)
        hrc = SetString(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "Module", pszModule);
    if (SUCCEEDED(hrc) && argc >= 5)
        hrc = SetUInt64(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "ModuleAddress", ModuleAddress);
    if (SUCCEEDED(hrc) && argc >= 6)
        hrc = SetUInt64(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "ModuleSize", ModuleSize);

    return FAILED(hrc);
}


static DECLCALLBACK(void) handleVDError(void *pvUser, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    RTMsgErrorV(pszFormat, va);
    RTMsgError("Error code %Rrc at %s(%u) in function %s", rc, RT_SRC_POS_ARGS);
}

static int handleVDMessage(void *pvUser, const char *pszFormat, va_list va)
{
    NOREF(pvUser);
    return RTPrintfV(pszFormat, va);
}

static int CmdSetHDUUID(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    Guid uuid;
    RTUUID rtuuid;
    enum eUuidType {
        HDUUID,
        HDPARENTUUID
    } uuidType;

    if (!strcmp(argv[0], "sethduuid"))
    {
        uuidType = HDUUID;
        if (argc != 3 && argc != 2)
            return errorSyntax(USAGE_SETHDUUID, "Not enough parameters");
        /* if specified, take UUID, otherwise generate a new one */
        if (argc == 3)
        {
            if (RT_FAILURE(RTUuidFromStr(&rtuuid, argv[2])))
                return errorSyntax(USAGE_SETHDUUID, "Invalid UUID parameter");
            uuid = argv[2];
        } else
            uuid.create();
    }
    else if (!strcmp(argv[0], "sethdparentuuid"))
    {
        uuidType = HDPARENTUUID;
        if (argc != 3)
            return errorSyntax(USAGE_SETHDPARENTUUID, "Not enough parameters");
        if (RT_FAILURE(RTUuidFromStr(&rtuuid, argv[2])))
            return errorSyntax(USAGE_SETHDPARENTUUID, "Invalid UUID parameter");
        uuid = argv[2];
    }
    else
        return errorSyntax(USAGE_SETHDUUID, "Invalid invocation");

    /* just try it */
    char *pszFormat = NULL;
    int rc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                         argv[1], &pszFormat);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Format autodetect failed: %Rrc", rc);
        return 1;
    }

    PVBOXHDD pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;
    vdInterfaceErrorCallbacks.pfnMessage   = handleVDMessage;

    rc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                        &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(rc);

    rc = VDCreate(pVDIfs, &pDisk);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Cannot create the virtual disk container: %Rrc", rc);
        return 1;
    }

    /* Open the image */
    rc = VDOpen(pDisk, pszFormat, argv[1], VD_OPEN_FLAGS_NORMAL, NULL);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Cannot open the image: %Rrc", rc);
        return 1;
    }

    if (uuidType == HDUUID)
      rc = VDSetUuid(pDisk, VD_LAST_IMAGE, uuid.raw());
    else
      rc = VDSetParentUuid(pDisk, VD_LAST_IMAGE, uuid.raw());
    if (RT_FAILURE(rc))
        RTMsgError("Cannot set a new UUID: %Rrc", rc);
    else
        RTPrintf("UUID changed to: %s\n", uuid.toString().c_str());

    VDCloseAll(pDisk);

    return RT_FAILURE(rc);
}


static int CmdDumpHDInfo(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    /* we need exactly one parameter: the image file */
    if (argc != 1)
    {
        return errorSyntax(USAGE_DUMPHDINFO, "Not enough parameters");
    }

    /* just try it */
    char *pszFormat = NULL;
    int rc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                         argv[0], &pszFormat);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Format autodetect failed: %Rrc", rc);
        return 1;
    }

    PVBOXHDD pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;
    vdInterfaceErrorCallbacks.pfnMessage   = handleVDMessage;

    rc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                        &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(rc);

    rc = VDCreate(pVDIfs, &pDisk);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Cannot create the virtual disk container: %Rrc", rc);
        return 1;
    }

    /* Open the image */
    rc = VDOpen(pDisk, pszFormat, argv[0], VD_OPEN_FLAGS_INFO, NULL);
    if (RT_FAILURE(rc))
    {
        RTMsgError("Cannot open the image: %Rrc", rc);
        return 1;
    }

    VDDumpImages(pDisk);

    VDCloseAll(pDisk);

    return RT_FAILURE(rc);
}

static int partRead(RTFILE File, PHOSTPARTITIONS pPart)
{
    uint8_t aBuffer[512];
    int rc;

    pPart->cPartitions = 0;
    memset(pPart->aPartitions, '\0', sizeof(pPart->aPartitions));
    rc = RTFileReadAt(File, 0, &aBuffer, sizeof(aBuffer), NULL);
    if (RT_FAILURE(rc))
        return rc;
    if (aBuffer[510] != 0x55 || aBuffer[511] != 0xaa)
        return VERR_INVALID_PARAMETER;

    unsigned uExtended = (unsigned)-1;

    for (unsigned i = 0; i < 4; i++)
    {
        uint8_t *p = &aBuffer[0x1be + i * 16];
        if (p[4] == 0)
            continue;
        PHOSTPARTITION pCP = &pPart->aPartitions[pPart->cPartitions++];
        pCP->uIndex = i + 1;
        pCP->uType = p[4];
        pCP->uStartCylinder = (uint32_t)p[3] + ((uint32_t)(p[2] & 0xc0) << 2);
        pCP->uStartHead = p[1];
        pCP->uStartSector = p[2] & 0x3f;
        pCP->uEndCylinder = (uint32_t)p[7] + ((uint32_t)(p[6] & 0xc0) << 2);
        pCP->uEndHead = p[5];
        pCP->uEndSector = p[6] & 0x3f;
        pCP->uStart = RT_MAKE_U32_FROM_U8(p[8], p[9], p[10], p[11]);
        pCP->uSize = RT_MAKE_U32_FROM_U8(p[12], p[13], p[14], p[15]);
        pCP->uPartDataStart = 0;    /* will be filled out later properly. */
        pCP->cPartDataSectors = 0;

        if (PARTTYPE_IS_EXTENDED(p[4]))
        {
            if (uExtended == (unsigned)-1)
                uExtended = (unsigned)(pCP - pPart->aPartitions);
            else
            {
                RTMsgError("More than one extended partition");
                return VERR_INVALID_PARAMETER;
            }
        }
    }

    if (uExtended != (unsigned)-1)
    {
        unsigned uIndex = 5;
        uint64_t uStart = pPart->aPartitions[uExtended].uStart;
        uint64_t uOffset = 0;
        if (!uStart)
        {
            RTMsgError("Inconsistency for logical partition start");
            return VERR_INVALID_PARAMETER;
        }

        do
        {
            rc = RTFileReadAt(File, (uStart + uOffset) * 512, &aBuffer, sizeof(aBuffer), NULL);
            if (RT_FAILURE(rc))
                return rc;

            if (aBuffer[510] != 0x55 || aBuffer[511] != 0xaa)
            {
                RTMsgError("Logical partition without magic");
                return VERR_INVALID_PARAMETER;
            }
            uint8_t *p = &aBuffer[0x1be];

            if (p[4] == 0)
            {
                RTMsgError("Logical partition with type 0 encountered");
                return VERR_INVALID_PARAMETER;
            }

            PHOSTPARTITION pCP = &pPart->aPartitions[pPart->cPartitions++];
            pCP->uIndex = uIndex;
            pCP->uType = p[4];
            pCP->uStartCylinder = (uint32_t)p[3] + ((uint32_t)(p[2] & 0xc0) << 2);
            pCP->uStartHead = p[1];
            pCP->uStartSector = p[2] & 0x3f;
            pCP->uEndCylinder = (uint32_t)p[7] + ((uint32_t)(p[6] & 0xc0) << 2);
            pCP->uEndHead = p[5];
            pCP->uEndSector = p[6] & 0x3f;
            uint32_t uStartOffset = RT_MAKE_U32_FROM_U8(p[8], p[9], p[10], p[11]);
            if (!uStartOffset)
            {
                RTMsgError("Invalid partition start offset");
                return VERR_INVALID_PARAMETER;
            }
            pCP->uStart = uStart + uOffset + uStartOffset;
            pCP->uSize = RT_MAKE_U32_FROM_U8(p[12], p[13], p[14], p[15]);
            /* Fill out partitioning location info for EBR. */
            pCP->uPartDataStart = uStart + uOffset;
            pCP->cPartDataSectors = uStartOffset;
            p += 16;
            if (p[4] == 0)
                uExtended = (unsigned)-1;
            else if (PARTTYPE_IS_EXTENDED(p[4]))
            {
                uExtended = uIndex++;
                uOffset = RT_MAKE_U32_FROM_U8(p[8], p[9], p[10], p[11]);
            }
            else
            {
                RTMsgError("Logical partition chain broken");
                return VERR_INVALID_PARAMETER;
            }
        } while (uExtended != (unsigned)-1);
    }

    /* Sort partitions in ascending order of start sector, plus a trivial
     * bit of consistency checking. */
    for (unsigned i = 0; i < pPart->cPartitions-1; i++)
    {
        unsigned uMinIdx = i;
        uint64_t uMinVal = pPart->aPartitions[i].uStart;
        for (unsigned j = i + 1; j < pPart->cPartitions; j++)
        {
            if (pPart->aPartitions[j].uStart < uMinVal)
            {
                uMinIdx = j;
                uMinVal = pPart->aPartitions[j].uStart;
            }
            else if (pPart->aPartitions[j].uStart == uMinVal)
            {
                RTMsgError("Two partitions start at the same place");
                return VERR_INVALID_PARAMETER;
            }
            else if (pPart->aPartitions[j].uStart == 0)
            {
                RTMsgError("Partition starts at sector 0");
                return VERR_INVALID_PARAMETER;
            }
        }
        if (uMinIdx != i)
        {
            /* Swap entries at index i and uMinIdx. */
            memcpy(&pPart->aPartitions[pPart->cPartitions],
                   &pPart->aPartitions[i], sizeof(HOSTPARTITION));
            memcpy(&pPart->aPartitions[i],
                   &pPart->aPartitions[uMinIdx], sizeof(HOSTPARTITION));
            memcpy(&pPart->aPartitions[uMinIdx],
                   &pPart->aPartitions[pPart->cPartitions], sizeof(HOSTPARTITION));
        }
    }

    /* Fill out partitioning location info for MBR. */
    pPart->aPartitions[0].uPartDataStart = 0;
    pPart->aPartitions[0].cPartDataSectors = pPart->aPartitions[0].uStart;

    /* Now do a some partition table consistency checking, to reject the most
     * obvious garbage which can lead to trouble later. */
    uint64_t uPrevEnd = 0;
    for (unsigned i = 0; i < pPart->cPartitions-1; i++)
    {
        if (pPart->aPartitions[i].cPartDataSectors)
            uPrevEnd = pPart->aPartitions[i].uPartDataStart + pPart->aPartitions[i].cPartDataSectors;
        if (pPart->aPartitions[i].uStart < uPrevEnd)
        {
            RTMsgError("Overlapping partitions");
            return VERR_INVALID_PARAMETER;
        }
        if (!PARTTYPE_IS_EXTENDED(pPart->aPartitions[i].uType))
            uPrevEnd = pPart->aPartitions[i].uStart + pPart->aPartitions[i].uSize;
    }

    return VINF_SUCCESS;
}

static int CmdListPartitions(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    Utf8Str rawdisk;

    /* let's have a closer look at the arguments */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-rawdisk") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            rawdisk = argv[i];
        }
        else
        {
            return errorSyntax(USAGE_LISTPARTITIONS, "Invalid parameter '%s'", argv[i]);
        }
    }

    if (rawdisk.isEmpty())
        return errorSyntax(USAGE_LISTPARTITIONS, "Mandatory parameter -rawdisk missing");

    RTFILE RawFile;
    int vrc = RTFileOpen(&RawFile, rawdisk.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(vrc))
    {
        RTMsgError("Cannnot open the raw disk: %Rrc", vrc);
        return vrc;
    }

    HOSTPARTITIONS partitions;
    vrc = partRead(RawFile, &partitions);
    /* Don't bail out on errors, print the table and return the result code. */

    RTPrintf("Number  Type   StartCHS       EndCHS      Size (MiB)  Start (Sect)\n");
    for (unsigned i = 0; i < partitions.cPartitions; i++)
    {
        /* Don't show the extended partition, otherwise users might think they
         * can add it to the list of partitions for raw partition access. */
        if (PARTTYPE_IS_EXTENDED(partitions.aPartitions[i].uType))
            continue;

        RTPrintf("%-7u %#04x  %-4u/%-3u/%-2u  %-4u/%-3u/%-2u    %10llu   %10llu\n",
                 partitions.aPartitions[i].uIndex,
                 partitions.aPartitions[i].uType,
                 partitions.aPartitions[i].uStartCylinder,
                 partitions.aPartitions[i].uStartHead,
                 partitions.aPartitions[i].uStartSector,
                 partitions.aPartitions[i].uEndCylinder,
                 partitions.aPartitions[i].uEndHead,
                 partitions.aPartitions[i].uEndSector,
                 partitions.aPartitions[i].uSize / 2048,
                 partitions.aPartitions[i].uStart);
    }

    return vrc;
}

static PVBOXHDDRAWPARTDESC appendPartDesc(uint32_t *pcPartDescs, PVBOXHDDRAWPARTDESC *ppPartDescs)
{
    (*pcPartDescs)++;
    PVBOXHDDRAWPARTDESC p;
    p = (PVBOXHDDRAWPARTDESC)RTMemRealloc(*ppPartDescs,
                                          *pcPartDescs * sizeof(VBOXHDDRAWPARTDESC));
    *ppPartDescs = p;
    if (p)
    {
        p = p + *pcPartDescs - 1;
        memset(p, '\0', sizeof(VBOXHDDRAWPARTDESC));
    }

    return p;
}

static int CmdCreateRawVMDK(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    HRESULT rc = S_OK;
    Utf8Str filename;
    const char *pszMBRFilename = NULL;
    Utf8Str rawdisk;
    const char *pszPartitions = NULL;
    bool fRegister = false;
    bool fRelative = false;

    uint64_t cbSize = 0;
    PVBOXHDD pDisk = NULL;
    VBOXHDDRAW RawDescriptor;
    PVDINTERFACE pVDIfs = NULL;

    /* let's have a closer look at the arguments */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-filename") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            filename = argv[i];
        }
        else if (strcmp(argv[i], "-mbr") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            pszMBRFilename = argv[i];
        }
        else if (strcmp(argv[i], "-rawdisk") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            rawdisk = argv[i];
        }
        else if (strcmp(argv[i], "-partitions") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            pszPartitions = argv[i];
        }
        else if (strcmp(argv[i], "-register") == 0)
        {
            fRegister = true;
        }
#ifdef RT_OS_LINUX
        else if (strcmp(argv[i], "-relative") == 0)
        {
            fRelative = true;
        }
#endif /* RT_OS_LINUX */
        else
            return errorSyntax(USAGE_CREATERAWVMDK, "Invalid parameter '%s'", argv[i]);
    }

    if (filename.isEmpty())
        return errorSyntax(USAGE_CREATERAWVMDK, "Mandatory parameter -filename missing");
    if (rawdisk.isEmpty())
        return errorSyntax(USAGE_CREATERAWVMDK, "Mandatory parameter -rawdisk missing");
    if (!pszPartitions && pszMBRFilename)
        return errorSyntax(USAGE_CREATERAWVMDK, "The parameter -mbr is only valid when the parameter -partitions is also present");

#ifdef RT_OS_DARWIN
    fRelative = true;
#endif /* RT_OS_DARWIN */
    RTFILE RawFile;
    int vrc = RTFileOpen(&RawFile, rawdisk.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(vrc))
    {
        RTMsgError("Cannot open the raw disk '%s': %Rrc", rawdisk.c_str(), vrc);
        goto out;
    }

#ifdef RT_OS_WINDOWS
    /* Windows NT has no IOCTL_DISK_GET_LENGTH_INFORMATION ioctl. This was
     * added to Windows XP, so we have to use the available info from DriveGeo.
     * Note that we cannot simply use IOCTL_DISK_GET_DRIVE_GEOMETRY as it
     * yields a slightly different result than IOCTL_DISK_GET_LENGTH_INFO.
     * We call IOCTL_DISK_GET_DRIVE_GEOMETRY first as we need to check the media
     * type anyway, and if IOCTL_DISK_GET_LENGTH_INFORMATION is supported
     * we will later override cbSize.
     */
    DISK_GEOMETRY DriveGeo;
    DWORD cbDriveGeo;
    if (DeviceIoControl((HANDLE)RawFile,
                        IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                        &DriveGeo, sizeof(DriveGeo), &cbDriveGeo, NULL))
    {
        if (   DriveGeo.MediaType == FixedMedia
            || DriveGeo.MediaType == RemovableMedia)
        {
            cbSize =     DriveGeo.Cylinders.QuadPart
                     *   DriveGeo.TracksPerCylinder
                     *   DriveGeo.SectorsPerTrack
                     *   DriveGeo.BytesPerSector;
        }
        else
        {
            RTMsgError("File '%s' is no fixed/removable medium device", rawdisk.c_str());
            vrc = VERR_INVALID_PARAMETER;
            goto out;
        }

        GET_LENGTH_INFORMATION DiskLenInfo;
        DWORD junk;
        if (DeviceIoControl((HANDLE)RawFile,
                            IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                            &DiskLenInfo, sizeof(DiskLenInfo), &junk, (LPOVERLAPPED)NULL))
        {
            /* IOCTL_DISK_GET_LENGTH_INFO is supported -- override cbSize. */
            cbSize = DiskLenInfo.Length.QuadPart;
        }
    }
    else
    {
        vrc = RTErrConvertFromWin32(GetLastError());
        RTMsgError("Cannot get the geometry of the raw disk '%s': %Rrc", rawdisk.c_str(), vrc);
        goto out;
    }
#elif defined(RT_OS_LINUX)
    struct stat DevStat;
    if (!fstat(RawFile, &DevStat) && S_ISBLK(DevStat.st_mode))
    {
#ifdef BLKGETSIZE64
        /* BLKGETSIZE64 is broken up to 2.4.17 and in many 2.5.x. In 2.6.0
         * it works without problems. */
        struct utsname utsname;
        if (    uname(&utsname) == 0
            &&  (   (strncmp(utsname.release, "2.5.", 4) == 0 && atoi(&utsname.release[4]) >= 18)
                 || (strncmp(utsname.release, "2.", 2) == 0 && atoi(&utsname.release[2]) >= 6)))
        {
            uint64_t cbBlk;
            if (!ioctl(RawFile, BLKGETSIZE64, &cbBlk))
                cbSize = cbBlk;
        }
#endif /* BLKGETSIZE64 */
        if (!cbSize)
        {
            long cBlocks;
            if (!ioctl(RawFile, BLKGETSIZE, &cBlocks))
                cbSize = (uint64_t)cBlocks << 9;
            else
            {
                vrc = RTErrConvertFromErrno(errno);
                RTMsgError("Cannot get the size of the raw disk '%s': %Rrc", rawdisk.c_str(), vrc);
                goto out;
            }
        }
    }
    else
    {
        RTMsgError("File '%s' is no block device", rawdisk.c_str());
        vrc = VERR_INVALID_PARAMETER;
        goto out;
    }
#elif defined(RT_OS_DARWIN)
    struct stat DevStat;
    if (!fstat(RawFile, &DevStat) && S_ISBLK(DevStat.st_mode))
    {
        uint64_t cBlocks;
        uint32_t cbBlock;
        if (!ioctl(RawFile, DKIOCGETBLOCKCOUNT, &cBlocks))
        {
            if (!ioctl(RawFile, DKIOCGETBLOCKSIZE, &cbBlock))
                cbSize = cBlocks * cbBlock;
            else
            {
                RTMsgError("Cannot get the block size for file '%s': %Rrc", rawdisk.c_str(), vrc);
                vrc = RTErrConvertFromErrno(errno);
                goto out;
            }
        }
        else
        {
            vrc = RTErrConvertFromErrno(errno);
            RTMsgError("Cannot get the block count for file '%s': %Rrc", rawdisk.c_str(), vrc);
            goto out;
        }
    }
    else
    {
        RTMsgError("File '%s' is no block device", rawdisk.c_str());
        vrc = VERR_INVALID_PARAMETER;
        goto out;
    }
#elif defined(RT_OS_SOLARIS)
    struct stat DevStat;
    if (!fstat(RawFile, &DevStat) && (   S_ISBLK(DevStat.st_mode)
                                      || S_ISCHR(DevStat.st_mode)))
    {
        struct dk_minfo mediainfo;
        if (!ioctl(RawFile, DKIOCGMEDIAINFO, &mediainfo))
            cbSize = mediainfo.dki_capacity * mediainfo.dki_lbsize;
        else
        {
            vrc = RTErrConvertFromErrno(errno);
            RTMsgError("Cannot get the size of the raw disk '%s': %Rrc", rawdisk.c_str(), vrc);
            goto out;
        }
    }
    else
    {
        RTMsgError("File '%s' is no block or char device", rawdisk.c_str());
        vrc = VERR_INVALID_PARAMETER;
        goto out;
    }
#elif defined(RT_OS_FREEBSD)
    struct stat DevStat;
    if (!fstat(RawFile, &DevStat) && S_ISCHR(DevStat.st_mode))
    {
        off_t cbMedia = 0;
        if (!ioctl(RawFile, DIOCGMEDIASIZE, &cbMedia))
        {
            cbSize = cbMedia;
        }
        else
        {
            vrc = RTErrConvertFromErrno(errno);
            RTMsgError("Cannot get the block count for file '%s': %Rrc", rawdisk.c_str(), vrc);
            goto out;
        }
    }
    else
    {
        RTMsgError("File '%s' is no character device", rawdisk.c_str());
        vrc = VERR_INVALID_PARAMETER;
        goto out;
    }
#else /* all unrecognized OSes */
    /* Hopefully this works on all other hosts. If it doesn't, it'll just fail
     * creating the VMDK, so no real harm done. */
    vrc = RTFileGetSize(RawFile, &cbSize);
    if (RT_FAILURE(vrc))
    {
        RTMsgError("Cannot get the size of the raw disk '%s': %Rrc", rawdisk.c_str(), vrc);
        goto out;
    }
#endif

    /* Check whether cbSize is actually sensible. */
    if (!cbSize || cbSize % 512)
    {
        RTMsgError("Detected size of raw disk '%s' is %s, an invalid value", rawdisk.c_str(), cbSize);
        vrc = VERR_INVALID_PARAMETER;
        goto out;
    }

    RawDescriptor.szSignature[0] = 'R';
    RawDescriptor.szSignature[1] = 'A';
    RawDescriptor.szSignature[2] = 'W';
    RawDescriptor.szSignature[3] = '\0';
    if (!pszPartitions)
    {
        RawDescriptor.fRawDisk = true;
        RawDescriptor.pszRawDisk = rawdisk.c_str();
    }
    else
    {
        RawDescriptor.fRawDisk = false;
        RawDescriptor.pszRawDisk = NULL;
        RawDescriptor.cPartDescs = 0;
        RawDescriptor.pPartDescs = NULL;

        uint32_t uPartitions = 0;

        const char *p = pszPartitions;
        char *pszNext;
        uint32_t u32;
        while (*p != '\0')
        {
            vrc = RTStrToUInt32Ex(p, &pszNext, 0, &u32);
            if (RT_FAILURE(vrc))
            {
                RTMsgError("Incorrect value in partitions parameter");
                goto out;
            }
            uPartitions |= RT_BIT(u32);
            p = pszNext;
            if (*p == ',')
                p++;
            else if (*p != '\0')
            {
                RTMsgError("Incorrect separator in partitions parameter");
                vrc = VERR_INVALID_PARAMETER;
                goto out;
            }
        }

        HOSTPARTITIONS partitions;
        vrc = partRead(RawFile, &partitions);
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Cannot read the partition information from '%s'", rawdisk.c_str());
            goto out;
        }

        for (unsigned i = 0; i < partitions.cPartitions; i++)
        {
            if (    uPartitions & RT_BIT(partitions.aPartitions[i].uIndex)
                &&  PARTTYPE_IS_EXTENDED(partitions.aPartitions[i].uType))
            {
                /* Some ignorant user specified an extended partition.
                 * Bad idea, as this would trigger an overlapping
                 * partitions error later during VMDK creation. So warn
                 * here and ignore what the user requested. */
                RTMsgWarning("It is not possible (and necessary) to explicitly give access to the "
                             "extended partition %u. If required, enable access to all logical "
                             "partitions inside this extended partition.",
                             partitions.aPartitions[i].uIndex);
                uPartitions &= ~RT_BIT(partitions.aPartitions[i].uIndex);
            }
        }

        for (unsigned i = 0; i < partitions.cPartitions; i++)
        {
            PVBOXHDDRAWPARTDESC pPartDesc = NULL;

            /* first dump the MBR/EPT data area */
            if (partitions.aPartitions[i].cPartDataSectors)
            {
                pPartDesc = appendPartDesc(&RawDescriptor.cPartDescs,
                                           &RawDescriptor.pPartDescs);
                if (!pPartDesc)
                {
                    RTMsgError("Out of memory allocating the partition list for '%s'", rawdisk.c_str());
                    vrc = VERR_NO_MEMORY;
                    goto out;
                }

                /** @todo the clipping below isn't 100% accurate, as it should
                 * actually clip to the track size. However that's easier said
                 * than done as figuring out the track size is heuristics. In
                 * any case the clipping is adjusted later after sorting, to
                 * prevent overlapping data areas on the resulting image. */
                pPartDesc->cbData = RT_MIN(partitions.aPartitions[i].cPartDataSectors, 63) * 512;
                pPartDesc->uStart = partitions.aPartitions[i].uPartDataStart * 512;
                Assert(pPartDesc->cbData - (size_t)pPartDesc->cbData == 0);
                void *pPartData = RTMemAlloc((size_t)pPartDesc->cbData);
                if (!pPartData)
                {
                    RTMsgError("Out of memory allocating the partition descriptor for '%s'", rawdisk.c_str());
                    vrc = VERR_NO_MEMORY;
                    goto out;
                }
                vrc = RTFileReadAt(RawFile, partitions.aPartitions[i].uPartDataStart * 512,
                                   pPartData, (size_t)pPartDesc->cbData, NULL);
                if (RT_FAILURE(vrc))
                {
                    RTMsgError("Cannot read partition data from raw device '%s': %Rrc", rawdisk.c_str(), vrc);
                    goto out;
                }
                /* Splice in the replacement MBR code if specified. */
                if (    partitions.aPartitions[i].uPartDataStart == 0
                    &&  pszMBRFilename)
                {
                    RTFILE MBRFile;
                    vrc = RTFileOpen(&MBRFile, pszMBRFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                    if (RT_FAILURE(vrc))
                    {
                        RTMsgError("Cannot open replacement MBR file '%s' specified with -mbr: %Rrc", pszMBRFilename, vrc);
                        goto out;
                    }
                    vrc = RTFileReadAt(MBRFile, 0, pPartData, 0x1be, NULL);
                    RTFileClose(MBRFile);
                    if (RT_FAILURE(vrc))
                    {
                        RTMsgError("Cannot read replacement MBR file '%s': %Rrc", pszMBRFilename, vrc);
                        goto out;
                    }
                }
                pPartDesc->pvPartitionData = pPartData;
            }

            if (PARTTYPE_IS_EXTENDED(partitions.aPartitions[i].uType))
            {
                /* Suppress exporting the actual extended partition. Only
                 * logical partitions should be processed. However completely
                 * ignoring it leads to leaving out the EBR data. */
                continue;
            }

            /* set up values for non-relative device names */
            const char *pszRawName = rawdisk.c_str();
            uint64_t uStartOffset = partitions.aPartitions[i].uStart * 512;

            pPartDesc = appendPartDesc(&RawDescriptor.cPartDescs,
                                       &RawDescriptor.pPartDescs);
            if (!pPartDesc)
            {
                RTMsgError("Out of memory allocating the partition list for '%s'", rawdisk.c_str());
                vrc = VERR_NO_MEMORY;
                goto out;
            }

            if (uPartitions & RT_BIT(partitions.aPartitions[i].uIndex))
            {
                if (fRelative)
                {
#ifdef RT_OS_LINUX
                    /* Refer to the correct partition and use offset 0. */
                    char *psz;
                    RTStrAPrintf(&psz, "%s%u", rawdisk.c_str(),
                                 partitions.aPartitions[i].uIndex);
                    if (!psz)
                    {
                        vrc = VERR_NO_STR_MEMORY;
                        RTMsgError("Cannot create reference to individual partition %u, rc=%Rrc",
                                   partitions.aPartitions[i].uIndex, vrc);
                        goto out;
                    }
                    pszRawName = psz;
                    uStartOffset = 0;
#elif defined(RT_OS_DARWIN)
                    /* Refer to the correct partition and use offset 0. */
                    char *psz;
                    RTStrAPrintf(&psz, "%ss%u", rawdisk.c_str(),
                                 partitions.aPartitions[i].uIndex);
                    if (!psz)
                    {
                        vrc = VERR_NO_STR_MEMORY;
                        RTMsgError("Cannot create reference to individual partition %u, rc=%Rrc",
                                 partitions.aPartitions[i].uIndex, vrc);
                        goto out;
                    }
                    pszRawName = psz;
                    uStartOffset = 0;
#else
                    /** @todo not implemented for other hosts. Treat just like
                     * not specified (this code is actually never reached). */
#endif
                }

                pPartDesc->pszRawDevice = pszRawName;
                pPartDesc->uStartOffset = uStartOffset;
            }
            else
            {
                pPartDesc->pszRawDevice = NULL;
                pPartDesc->uStartOffset = 0;
            }

            pPartDesc->uStart = partitions.aPartitions[i].uStart * 512;
            pPartDesc->cbData = partitions.aPartitions[i].uSize * 512;
        }

        /* Sort data areas in ascending order of start. */
        for (unsigned i = 0; i < RawDescriptor.cPartDescs-1; i++)
        {
            unsigned uMinIdx = i;
            uint64_t uMinVal = RawDescriptor.pPartDescs[i].uStart;
            for (unsigned j = i + 1; j < RawDescriptor.cPartDescs; j++)
            {
                if (RawDescriptor.pPartDescs[j].uStart < uMinVal)
                {
                    uMinIdx = j;
                    uMinVal = RawDescriptor.pPartDescs[j].uStart;
                }
            }
            if (uMinIdx != i)
            {
                /* Swap entries at index i and uMinIdx. */
                VBOXHDDRAWPARTDESC tmp;
                memcpy(&tmp, &RawDescriptor.pPartDescs[i], sizeof(tmp));
                memcpy(&RawDescriptor.pPartDescs[i], &RawDescriptor.pPartDescs[uMinIdx], sizeof(tmp));
                memcpy(&RawDescriptor.pPartDescs[uMinIdx], &tmp, sizeof(tmp));
            }
        }

        /* Have a second go at MBR/EPT area clipping. Now that the data areas
         * are sorted this is much easier to get 100% right. */
        for (unsigned i = 0; i < RawDescriptor.cPartDescs-1; i++)
        {
            if (RawDescriptor.pPartDescs[i].pvPartitionData)
            {
                RawDescriptor.pPartDescs[i].cbData = RT_MIN(RawDescriptor.pPartDescs[i+1].uStart - RawDescriptor.pPartDescs[i].uStart, RawDescriptor.pPartDescs[i].cbData);
                if (!RawDescriptor.pPartDescs[i].cbData)
                {
                    RTMsgError("MBR/EPT overlaps with data area");
                    vrc = VERR_INVALID_PARAMETER;
                    goto out;
                }
            }
        }
    }

    RTFileClose(RawFile);

#ifdef DEBUG_klaus
    RTPrintf("#            start         length    startoffset  partdataptr  device\n");
    for (unsigned i = 0; i < RawDescriptor.cPartDescs; i++)
    {
        RTPrintf("%2u  %14RU64 %14RU64 %14RU64 %#18p %s\n", i,
                 RawDescriptor.pPartDescs[i].uStart,
                 RawDescriptor.pPartDescs[i].cbData,
                 RawDescriptor.pPartDescs[i].uStartOffset,
                 RawDescriptor.pPartDescs[i].pvPartitionData,
                 RawDescriptor.pPartDescs[i].pszRawDevice);
    }
#endif

    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;
    vdInterfaceErrorCallbacks.pfnMessage   = handleVDMessage;

    vrc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(vrc);

    vrc = VDCreate(pVDIfs, &pDisk);
    if (RT_FAILURE(vrc))
    {
        RTMsgError("Cannot create the virtual disk container: %Rrc", vrc);
        goto out;
    }

    Assert(RT_MIN(cbSize / 512 / 16 / 63, 16383) -
           (unsigned int)RT_MIN(cbSize / 512 / 16 / 63, 16383) == 0);
    VDGEOMETRY PCHS, LCHS;
    PCHS.cCylinders = (unsigned int)RT_MIN(cbSize / 512 / 16 / 63, 16383);
    PCHS.cHeads = 16;
    PCHS.cSectors = 63;
    LCHS.cCylinders = 0;
    LCHS.cHeads = 0;
    LCHS.cSectors = 0;
    vrc = VDCreateBase(pDisk, "VMDK", filename.c_str(), cbSize,
                       VD_IMAGE_FLAGS_FIXED | VD_VMDK_IMAGE_FLAGS_RAWDISK,
                       (char *)&RawDescriptor, &PCHS, &LCHS, NULL,
                       VD_OPEN_FLAGS_NORMAL, NULL, NULL);
    if (RT_FAILURE(vrc))
    {
        RTMsgError("Cannot create the raw disk VMDK: %Rrc", vrc);
        goto out;
    }
    RTPrintf("RAW host disk access VMDK file %s created successfully.\n", filename.c_str());

    VDCloseAll(pDisk);

    /* Clean up allocated memory etc. */
    if (pszPartitions)
    {
        for (unsigned i = 0; i < RawDescriptor.cPartDescs; i++)
        {
            /* Free memory allocated for relative device name. */
            if (fRelative && RawDescriptor.pPartDescs[i].pszRawDevice)
                RTStrFree((char *)(void *)RawDescriptor.pPartDescs[i].pszRawDevice);
            if (RawDescriptor.pPartDescs[i].pvPartitionData)
                RTMemFree((void *)RawDescriptor.pPartDescs[i].pvPartitionData);
        }
        if (RawDescriptor.pPartDescs)
            RTMemFree(RawDescriptor.pPartDescs);
    }

    if (fRegister)
    {
        ComPtr<IMedium> hardDisk;
        CHECK_ERROR(aVirtualBox, OpenMedium(Bstr(filename).raw(),
                                            DeviceType_HardDisk,
                                            AccessMode_ReadWrite,
                                            hardDisk.asOutParam()));
    }

    return SUCCEEDED(rc) ? 0 : 1;

out:
    RTMsgError("The raw disk vmdk file was not created");
    return RT_SUCCESS(vrc) ? 0 : 1;
}

static int CmdRenameVMDK(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    Utf8Str src;
    Utf8Str dst;
    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-from") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            src = argv[i];
        }
        else if (strcmp(argv[i], "-to") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            dst = argv[i];
        }
        else
        {
            return errorSyntax(USAGE_RENAMEVMDK, "Invalid parameter '%s'", argv[i]);
        }
    }

    if (src.isEmpty())
        return errorSyntax(USAGE_RENAMEVMDK, "Mandatory parameter -from missing");
    if (dst.isEmpty())
        return errorSyntax(USAGE_RENAMEVMDK, "Mandatory parameter -to missing");

    PVBOXHDD pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;
    vdInterfaceErrorCallbacks.pfnMessage   = handleVDMessage;

    int vrc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                             &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(vrc);

    vrc = VDCreate(pVDIfs, &pDisk);
    if (RT_FAILURE(vrc))
    {
        RTMsgError("Cannot create the virtual disk container: %Rrc", vrc);
        return vrc;
    }
    else
    {
        vrc = VDOpen(pDisk, "VMDK", src.c_str(), VD_OPEN_FLAGS_NORMAL, NULL);
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Cannot create the source image: %Rrc", vrc);
        }
        else
        {
            vrc = VDCopy(pDisk, 0, pDisk, "VMDK", dst.c_str(), true, 0,
                         VD_IMAGE_FLAGS_NONE, NULL, VD_OPEN_FLAGS_NORMAL,
                         NULL, NULL, NULL);
            if (RT_FAILURE(vrc))
            {
                RTMsgError("Cannot rename the image: %Rrc", vrc);
            }
        }
    }
    VDCloseAll(pDisk);
    return vrc;
}

static int CmdConvertToRaw(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    Utf8Str srcformat;
    Utf8Str src;
    Utf8Str dst;
    bool fWriteToStdOut = false;

    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-format") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            srcformat = argv[i];
        }
        else if (src.isEmpty())
        {
            src = argv[i];
        }
        else if (dst.isEmpty())
        {
            dst = argv[i];
#ifdef ENABLE_CONVERT_RAW_TO_STDOUT
            if (!strcmp(argv[i], "stdout"))
                fWriteToStdOut = true;
#endif /* ENABLE_CONVERT_RAW_TO_STDOUT */
        }
        else
        {
            return errorSyntax(USAGE_CONVERTTORAW, "Invalid parameter '%s'", argv[i]);
        }
    }

    if (src.isEmpty())
        return errorSyntax(USAGE_CONVERTTORAW, "Mandatory filename parameter missing");
    if (dst.isEmpty())
        return errorSyntax(USAGE_CONVERTTORAW, "Mandatory outputfile parameter missing");

    PVBOXHDD pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;
    vdInterfaceErrorCallbacks.pfnMessage   = handleVDMessage;

    int vrc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                             &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(vrc);

    vrc = VDCreate(pVDIfs, &pDisk);
    if (RT_FAILURE(vrc))
    {
        RTMsgError("Cannot create the virtual disk container: %Rrc", vrc);
        return 1;
    }

    /* Open raw output file. */
    RTFILE outFile;
    vrc = VINF_SUCCESS;
    if (fWriteToStdOut)
        outFile = 1;
    else
        vrc = RTFileOpen(&outFile, dst.c_str(), RTFILE_O_WRITE | RTFILE_O_CREATE | RTFILE_O_DENY_ALL);
    if (RT_FAILURE(vrc))
    {
        VDCloseAll(pDisk);
        RTMsgError("Cannot create destination file \"%s\": %Rrc", dst.c_str(), vrc);
        return 1;
    }

    if (srcformat.isEmpty())
    {
        char *pszFormat = NULL;
        vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                          src.c_str(), &pszFormat);
        if (RT_FAILURE(vrc))
        {
            VDCloseAll(pDisk);
            if (!fWriteToStdOut)
            {
                RTFileClose(outFile);
                RTFileDelete(dst.c_str());
            }
            RTMsgError("No file format specified and autodetect failed - please specify format: %Rrc", vrc);
            return 1;
        }
        srcformat = pszFormat;
        RTStrFree(pszFormat);
    }
    vrc = VDOpen(pDisk, srcformat.c_str(), src.c_str(), VD_OPEN_FLAGS_READONLY, NULL);
    if (RT_FAILURE(vrc))
    {
        VDCloseAll(pDisk);
        if (!fWriteToStdOut)
        {
            RTFileClose(outFile);
            RTFileDelete(dst.c_str());
        }
        RTMsgError("Cannot open the source image: %Rrc", vrc);
        return 1;
    }

    uint64_t cbSize = VDGetSize(pDisk, VD_LAST_IMAGE);
    uint64_t offFile = 0;
#define RAW_BUFFER_SIZE _128K
    size_t cbBuf = RAW_BUFFER_SIZE;
    void *pvBuf = RTMemAlloc(cbBuf);
    if (pvBuf)
    {
        RTStrmPrintf(g_pStdErr, "Converting image \"%s\" with size %RU64 bytes (%RU64MB) to raw...\n", src.c_str(), cbSize, (cbSize + _1M - 1) / _1M);
        while (offFile < cbSize)
        {
            size_t cb = (size_t)RT_MIN(cbSize - offFile, cbBuf);
            vrc = VDRead(pDisk, offFile, pvBuf, cb);
            if (RT_FAILURE(vrc))
                break;
            vrc = RTFileWrite(outFile, pvBuf, cb, NULL);
            if (RT_FAILURE(vrc))
                break;
            offFile += cb;
        }
        if (RT_FAILURE(vrc))
        {
            VDCloseAll(pDisk);
            if (!fWriteToStdOut)
            {
                RTFileClose(outFile);
                RTFileDelete(dst.c_str());
            }
            RTMsgError("Cannot copy image data: %Rrc", vrc);
            return 1;
        }
    }
    else
    {
        vrc = VERR_NO_MEMORY;
        VDCloseAll(pDisk);
        if (!fWriteToStdOut)
        {
            RTFileClose(outFile);
            RTFileDelete(dst.c_str());
        }
        RTMsgError("Out of memory allocating read buffer");
        return 1;
    }

    if (!fWriteToStdOut)
        RTFileClose(outFile);
    VDCloseAll(pDisk);
    return 0;
}

static int CmdConvertHardDisk(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    Utf8Str srcformat;
    Utf8Str dstformat;
    Utf8Str src;
    Utf8Str dst;
    int vrc;
    PVBOXHDD pSrcDisk = NULL;
    PVBOXHDD pDstDisk = NULL;

    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-srcformat") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            srcformat = argv[i];
        }
        else if (strcmp(argv[i], "-dstformat") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument("Missing argument to '%s'", argv[i]);
            }
            i++;
            dstformat = argv[i];
        }
        else if (src.isEmpty())
        {
            src = argv[i];
        }
        else if (dst.isEmpty())
        {
            dst = argv[i];
        }
        else
        {
            return errorSyntax(USAGE_CONVERTHD, "Invalid parameter '%s'", argv[i]);
        }
    }

    if (src.isEmpty())
        return errorSyntax(USAGE_CONVERTHD, "Mandatory input image parameter missing");
    if (dst.isEmpty())
        return errorSyntax(USAGE_CONVERTHD, "Mandatory output image parameter missing");


    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACE      vdInterfaceError;
    VDINTERFACEERROR vdInterfaceErrorCallbacks;
    vdInterfaceErrorCallbacks.cbSize       = sizeof(VDINTERFACEERROR);
    vdInterfaceErrorCallbacks.enmInterface = VDINTERFACETYPE_ERROR;
    vdInterfaceErrorCallbacks.pfnError     = handleVDError;
    vdInterfaceErrorCallbacks.pfnMessage   = handleVDMessage;

    vrc = VDInterfaceAdd(&vdInterfaceError, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         &vdInterfaceErrorCallbacks, NULL, &pVDIfs);
    AssertRC(vrc);

    do
    {
        /* Try to determine input image format */
        if (srcformat.isEmpty())
        {
            char *pszFormat = NULL;
            vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                              src.c_str(), &pszFormat);
            if (RT_FAILURE(vrc))
            {
                RTMsgError("No file format specified and autodetect failed - please specify format: %Rrc", vrc);
                break;
            }
            srcformat = pszFormat;
            RTStrFree(pszFormat);
        }

        vrc = VDCreate(pVDIfs, &pSrcDisk);
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Cannot create the source virtual disk container: %Rrc", vrc);
            break;
        }

        /* Open the input image */
        vrc = VDOpen(pSrcDisk, srcformat.c_str(), src.c_str(), VD_OPEN_FLAGS_READONLY, NULL);
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Cannot open the source image: %Rrc", vrc);
            break;
        }

        /* Output format defaults to VDI */
        if (dstformat.isEmpty())
            dstformat = "VDI";

        vrc = VDCreate(pVDIfs, &pDstDisk);
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Cannot create the destination virtual disk container: %Rrc", vrc);
            break;
        }

        uint64_t cbSize = VDGetSize(pSrcDisk, VD_LAST_IMAGE);
        RTStrmPrintf(g_pStdErr, "Converting image \"%s\" with size %RU64 bytes (%RU64MB)...\n", src.c_str(), cbSize, (cbSize + _1M - 1) / _1M);

        /* Create the output image */
        vrc = VDCopy(pSrcDisk, VD_LAST_IMAGE, pDstDisk, dstformat.c_str(),
                     dst.c_str(), false, 0, VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED,
                     NULL, VD_OPEN_FLAGS_NORMAL, NULL, NULL, NULL);
        if (RT_FAILURE(vrc))
        {
            RTMsgError("Cannot copy the image: %Rrc", vrc);
            break;
        }
    }
    while (0);
    if (pDstDisk)
        VDCloseAll(pDstDisk);
    if (pSrcDisk)
        VDCloseAll(pSrcDisk);

    return RT_SUCCESS(vrc) ? 0 : 1;
}

/**
 * Unloads the neccessary driver.
 *
 * @returns VBox status code
 */
int CmdModUninstall(void)
{
    int rc;

    rc = SUPR3Uninstall();
    if (RT_SUCCESS(rc))
        return 0;
    if (rc == VERR_NOT_IMPLEMENTED)
        return 0;
    return E_FAIL;
}

/**
 * Loads the neccessary driver.
 *
 * @returns VBox status code
 */
int CmdModInstall(void)
{
    int rc;

    rc = SUPR3Install();
    if (RT_SUCCESS(rc))
        return 0;
    if (rc == VERR_NOT_IMPLEMENTED)
        return 0;
    return E_FAIL;
}

int CmdDebugLog(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    /*
     * The first parameter is the name or UUID of a VM with a direct session
     * that we wish to open.
     */
    if (argc < 1)
        return errorSyntax(USAGE_DEBUGLOG, "Missing VM name/UUID");

    ComPtr<IMachine> ptrMachine;
    HRESULT rc;
    CHECK_ERROR_RET(aVirtualBox, FindMachine(Bstr(argv[0]).raw(),
                                             ptrMachine.asOutParam()), 1);

    CHECK_ERROR_RET(ptrMachine, LockMachine(aSession, LockType_Shared), 1);

    /*
     * Get the debugger interface.
     */
    ComPtr<IConsole> ptrConsole;
    CHECK_ERROR_RET(aSession, COMGETTER(Console)(ptrConsole.asOutParam()), 1);

    ComPtr<IMachineDebugger> ptrDebugger;
    CHECK_ERROR_RET(ptrConsole, COMGETTER(Debugger)(ptrDebugger.asOutParam()), 1);

    /*
     * Parse the command.
     */
    bool                fEnablePresent = false;
    bool                fEnable        = false;
    bool                fFlagsPresent  = false;
    iprt::MiniString    strFlags;
    bool                fGroupsPresent = false;
    iprt::MiniString    strGroups;
    bool                fDestsPresent  = false;
    iprt::MiniString    strDests;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--disable",      'E', RTGETOPT_REQ_NOTHING },
        { "--enable",       'e', RTGETOPT_REQ_NOTHING },
        { "--flags",        'f', RTGETOPT_REQ_STRING  },
        { "--groups",       'g', RTGETOPT_REQ_STRING  },
        { "--destinations", 'd', RTGETOPT_REQ_STRING  }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'e':
                fEnablePresent = true;
                fEnable = true;
                break;

            case 'E':
                fEnablePresent = true;
                fEnable = false;
                break;

            case 'f':
                fFlagsPresent = true;
                if (*ValueUnion.psz)
                {
                    if (strFlags.isNotEmpty())
                        strFlags.append(' ');
                    strFlags.append(ValueUnion.psz);
                }
                break;

            case 'g':
                fGroupsPresent = true;
                if (*ValueUnion.psz)
                {
                    if (strGroups.isNotEmpty())
                        strGroups.append(' ');
                    strGroups.append(ValueUnion.psz);
                }
                break;

            case 'd':
                fDestsPresent = true;
                if (*ValueUnion.psz)
                {
                    if (strDests.isNotEmpty())
                        strDests.append(' ');
                    strDests.append(ValueUnion.psz);
                }
                break;

            default:
                return errorGetOpt(USAGE_DEBUGLOG , ch, &ValueUnion);
        }
    }

    /*
     * Do the job.
     */
    if (fEnablePresent && !fEnable)
        CHECK_ERROR_RET(ptrDebugger, COMSETTER(LogEnabled)(FALSE), 1);

    /** @todo flags, groups destination. */
    if (fFlagsPresent || fGroupsPresent || fDestsPresent)
        RTMsgWarning("One or more of the requested features are not implemented! Feel free to do this.");

    if (fEnablePresent && fEnable)
        CHECK_ERROR_RET(ptrDebugger, COMSETTER(LogEnabled)(TRUE), 1);
    return 0;
}

/**
 * Generate a SHA-256 password hash
 */
int CmdGeneratePasswordHash(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    /* one parameter, the password to hash */
    if (argc != 1)
        return errorSyntax(USAGE_PASSWORDHASH, "password to hash required");

    uint8_t abDigest[RTSHA256_HASH_SIZE];
    RTSha256(argv[0], strlen(argv[0]), abDigest);
    char pszDigest[RTSHA256_DIGEST_LEN + 1];
    RTSha256ToString(abDigest, pszDigest, sizeof(pszDigest));
    RTPrintf("Password hash: %s\n", pszDigest);

    return 0;
}

/**
 * Wrapper for handling internal commands
 */
int handleInternalCommands(HandlerArg *a)
{
    g_fInternalMode = true;

    /* at least a command is required */
    if (a->argc < 1)
        return errorSyntax(USAGE_ALL, "Command missing");

    /*
     * The 'string switch' on command name.
     */
    const char *pszCmd = a->argv[0];
    if (!strcmp(pszCmd, "loadsyms"))
        return CmdLoadSyms(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    //if (!strcmp(pszCmd, "unloadsyms"))
    //    return CmdUnloadSyms(argc - 1 , &a->argv[1]);
    if (!strcmp(pszCmd, "sethduuid") || !strcmp(pszCmd, "sethdparentuuid"))
        return CmdSetHDUUID(a->argc, &a->argv[0], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "dumphdinfo"))
        return CmdDumpHDInfo(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "listpartitions"))
        return CmdListPartitions(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "createrawvmdk"))
        return CmdCreateRawVMDK(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "renamevmdk"))
        return CmdRenameVMDK(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "converttoraw"))
        return CmdConvertToRaw(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "converthd"))
        return CmdConvertHardDisk(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "modinstall"))
        return CmdModInstall();
    if (!strcmp(pszCmd, "moduninstall"))
        return CmdModUninstall();
    if (!strcmp(pszCmd, "debuglog"))
        return CmdDebugLog(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "passwordhash"))
        return CmdGeneratePasswordHash(a->argc - 1, &a->argv[1], a->virtualBox, a->session);

    /* default: */
    return errorSyntax(USAGE_ALL, "Invalid command '%s'", a->argv[0]);
}

