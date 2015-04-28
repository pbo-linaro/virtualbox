/* $Id$ */
/** @file
 * IPRT Testcase - RTGetOptArgv*.
 */

/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/path.h>

#include <iprt/err.h>
#include <iprt/param.h>
#include <iprt/getopt.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static const struct
{
    /** The input string. */
    const char *pszInput;
    /** Separators, NULL if default. */
    const char *pszSeparators;
    /** The number of arguments. */
    int         cArgs;
    /** Expected argument vector. */
    const char *apszArgs[16];
    /** Expected quoted string, bourne shell. */
    const char *pszOutBourneSh;
    /** Expected quoted string, MSC CRT. */
    const char *pszOutMsCrt;
} g_aBourneTests[] =
{
    {
        "0 1 \"\"2'' '3' 4 5 '''''6' 7 8 9 10 11",
        NULL,
        12,
        {
             "0",
             "1",
             "2",
             "3",
             "4",
             "5",
             "6",
             "7",
             "8",
             "9",
             "10",
             "11",
             NULL, NULL, NULL, NULL,
        },
        "0 1 2 3 4 5 6 7 8 9 10 11",
        "0 1 2 3 4 5 6 7 8 9 10 11"
    },
    {
        "\t\" asdf \"  '\"'xyz  \"\t\"  '\n'  '\"'  \"'\"\n\r ",
        NULL,
        6,
        {
            " asdf ",
            "\"xyz",
            "\t",
            "\n",
            "\"",
            "\'",
            NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "' asdf ' '\"xyz' '\t' '\n' '\"' ''\"'\"''",
        "\" asdf \" \"\\\"xyz\" \"\t\" \"\n\" \"\\\"\" '"
    },
    {
        ":0::1::::2:3:4:5:",
        ":",
        6,
        {
            "0",
            "1",
            "2",
            "3",
            "4",
            "5",
            NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "0 1 2 3 4 5",
        "0 1 2 3 4 5"
    },
    {
        "0:1;2:3;4:5",
        ";;;;;;;;;;;;;;;;;;;;;;:",
        6,
        {
            "0",
            "1",
            "2",
            "3",
            "4",
            "5",
            NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "0 1 2 3 4 5",
        "0 1 2 3 4 5"
    },
    {
        "abcd 'a ' ' b' ' c '",
        NULL,
        4,
        {
            "abcd",
            "a ",
            " b",
            " c ",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "abcd 'a ' ' b' ' c '",
        "abcd \"a \" \" b\" \" c \""
    },
    {
        "'a\n\\b' 'de'\"'\"'fg' h ''\"'\"''",
        NULL,
        4,
        {
            "a\n\\b",
            "de'fg",
            "h",
            "'",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "'a\n\\b' 'de'\"'\"'fg' h ''\"'\"''",
        "\"a\n\\b\" de'fg h '"
    },
    {
        "arg1 \"arg2=\\\"zyx\\\"\"  'arg3=\\\\\\'",
        NULL,
        3,
        {
            "arg1",
            "arg2=\"zyx\"",
            "arg3=\\\\\\",
            NULL,  NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,  NULL, NULL, NULL, NULL,
        },
        "arg1 'arg2=\"zyx\"' 'arg3=\\\\\\'",
        "arg1 \"arg2=\\\"zyx\\\"\" arg3=\\\\\\"
    },
};


static void tst3(void)
{
    /*
     * Bourne shell round-tripping.
     */
    RTTestISub("Round-trips / BOURNE_SH");
    for (unsigned i = 0; i < RT_ELEMENTS(g_aBourneTests); i++)
    {
        /* First */
        char **papszArgs1 = NULL;
        int    cArgs1     = -1;
        int rc = RTGetOptArgvFromString(&papszArgs1, &cArgs1, g_aBourneTests[i].pszInput, g_aBourneTests[i].pszSeparators);
        if (rc == VINF_SUCCESS)
        {
            if (cArgs1 != g_aBourneTests[i].cArgs)
                RTTestIFailed("g_aBourneTests[%i]: #1=%d, expected %d", i, cArgs1, g_aBourneTests[i].cArgs);
            for (int iArg = 0; iArg < cArgs1; iArg++)
                if (strcmp(papszArgs1[iArg], g_aBourneTests[i].apszArgs[iArg]) != 0)
                    RTTestIFailed("g_aBourneTests[%i]/#1: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s', '%s'))",
                                  i, iArg, papszArgs1[iArg], g_aBourneTests[i].apszArgs[iArg],
                                  g_aBourneTests[i].pszInput, g_aBourneTests[i].pszSeparators);
            RTTESTI_CHECK_RETV(papszArgs1[cArgs1] == NULL);

            /* Second */
            char *pszArgs2 = NULL;
            rc = RTGetOptArgvToString(&pszArgs2, papszArgs1, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
            if (rc == VINF_SUCCESS)
            {
                if (strcmp(pszArgs2, g_aBourneTests[i].pszOutBourneSh))
                    RTTestIFailed("g_aBourneTests[%i]/2: '%s', expected '%s'", i, pszArgs2, g_aBourneTests[i].pszOutBourneSh);

                /*
                 * Third
                 */
                char **papszArgs3 = NULL;
                int    cArgs3     = -1;
                rc = RTGetOptArgvFromString(&papszArgs3, &cArgs3, pszArgs2, NULL);
                if (rc == VINF_SUCCESS)
                {
                    if (cArgs3 != g_aBourneTests[i].cArgs)
                        RTTestIFailed("g_aBourneTests[%i]/3: %d, expected %d", i, cArgs3, g_aBourneTests[i].cArgs);
                    for (int iArg = 0; iArg < cArgs3; iArg++)
                        if (strcmp(papszArgs3[iArg], g_aBourneTests[i].apszArgs[iArg]) != 0)
                            RTTestIFailed("g_aBourneTests[%i]/3: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s',))",
                                          i, iArg, papszArgs3[iArg], g_aBourneTests[i].apszArgs[iArg], pszArgs2);
                    RTTESTI_CHECK_RETV(papszArgs3[cArgs3] == NULL);

                    /*
                     * Fourth
                     */
                    char *pszArgs4 = NULL;
                    rc = RTGetOptArgvToString(&pszArgs4, papszArgs3, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
                    if (rc == VINF_SUCCESS)
                    {
                        if (strcmp(pszArgs4, pszArgs2))
                            RTTestIFailed("g_aBourneTests[%i]/4: '%s' does not match #4='%s'", i, pszArgs2, pszArgs4);
                        RTStrFree(pszArgs4);
                    }
                    else
                        RTTestIFailed("g_aBourneTests[%i]/4: RTGetOptArgvToString() -> %Rrc", i, rc);
                    RTGetOptArgvFree(papszArgs3);
                }
                else
                    RTTestIFailed("g_aBourneTests[%i]/3: RTGetOptArgvFromString() -> %Rrc", i, rc);
                RTStrFree(pszArgs2);
            }
            else
                RTTestIFailed("g_aBourneTests[%i]/2: RTGetOptArgvToString() -> %Rrc", i, rc);
            RTGetOptArgvFree(papszArgs1);
        }
        else
            RTTestIFailed("g_aBourneTests[%i]/1: RTGetOptArgvFromString(,,'%s', '%s') -> %Rrc",
                          i, g_aBourneTests[i].pszInput, g_aBourneTests[i].pszSeparators, rc);
    }

}


static void tst2(void)
{
    RTTestISub("RTGetOptArgvToString / MS_CRT");

    static const struct
    {
        const char * const      apszArgs[5];
        const char             *pszCmdLine;
    } s_aMscCrtTests[] =
    {
        {
            { "abcd", "a ", " b", " c ", NULL },
            "abcd \"a \" \" b\" \" c \""
        },
        {
            { "a\\\\\\b", "de fg", "h", NULL, NULL },
            "a\\\\\\b \"de fg\" h"
        },
        {
            { "a\\\"b", "c", "d", "\"", NULL },
            "\"a\\\\\\\"b\" c d \"\\\"\""
        },
        {
            { "a\\\\b c", "d", "e", " \\", NULL },
            "\"a\\\\b c\" d e \" \\\\\""
        },
    };

    for (size_t i = 0; i < RT_ELEMENTS(s_aMscCrtTests); i++)
    {
        char *pszCmdLine = NULL;
        int rc = RTGetOptArgvToString(&pszCmdLine, s_aMscCrtTests[i].apszArgs, RTGETOPTARGV_CNV_QUOTE_MS_CRT);
        RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
        if (strcmp(s_aMscCrtTests[i].pszCmdLine, pszCmdLine))
            RTTestIFailed("g_aTest[%i] failed:\n"
                          " got      '%s'\n"
                          " expected '%s'\n",
                          i, pszCmdLine, s_aMscCrtTests[i].pszCmdLine);
        RTStrFree(pszCmdLine);
    }

    for (size_t i = 0; i < RT_ELEMENTS(g_aBourneTests); i++)
    {
        char *pszCmdLine = NULL;
        int rc = RTGetOptArgvToString(&pszCmdLine, g_aBourneTests[i].apszArgs, RTGETOPTARGV_CNV_QUOTE_MS_CRT);
        RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
        if (strcmp(g_aBourneTests[i].pszOutMsCrt, pszCmdLine))
            RTTestIFailed("g_aBourneTests[%i] failed:\n"
                          " got      |%s|\n"
                          " expected |%s|\n",
                          i, pszCmdLine, g_aBourneTests[i].pszOutMsCrt);
        RTStrFree(pszCmdLine);
    }



    RTTestISub("RTGetOptArgvToString / BOURNE_SH");

    for (size_t i = 0; i < RT_ELEMENTS(g_aBourneTests); i++)
    {
        char *pszCmdLine = NULL;
        int rc = RTGetOptArgvToString(&pszCmdLine, g_aBourneTests[i].apszArgs, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
        RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
        if (strcmp(g_aBourneTests[i].pszOutBourneSh, pszCmdLine))
            RTTestIFailed("g_aBourneTests[%i] failed:\n"
                          " got      |%s|\n"
                          " expected |%s|\n",
                          i, pszCmdLine, g_aBourneTests[i].pszOutBourneSh);
        RTStrFree(pszCmdLine);
    }
}

static void tst1(void)
{
    RTTestISub("RTGetOptArgvFromString");
    char **papszArgs = NULL;
    int    cArgs = -1;
    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "", NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 0);
    RTTESTI_CHECK_RETV(papszArgs);
    RTTESTI_CHECK_RETV(!papszArgs[0]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "0 1 \"\"2'' '3' 4 5 '''''6' 7 8 9 10 11", NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 12);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], "0"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "1"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "2"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "3"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "4"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "5"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[6], "6"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[7], "7"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[8], "8"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[9], "9"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[10], "10"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[11], "11"));
    RTTESTI_CHECK_RETV(!papszArgs[12]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "\t\" asdf \"  '\"'xyz  \"\t\"  '\n'  '\"'  \"'\"\n\r ", NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 6);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], " asdf "));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "\"xyz"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "\t"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "\n"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "\""));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "\'"));
    RTTESTI_CHECK_RETV(!papszArgs[6]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, ":0::1::::2:3:4:5:", ":"), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 6);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], "0"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "1"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "2"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "3"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "4"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "5"));
    RTTESTI_CHECK_RETV(!papszArgs[6]);
    RTGetOptArgvFree(papszArgs);

    RTTESTI_CHECK_RC_RETV(RTGetOptArgvFromString(&papszArgs, &cArgs, "0:1;2:3;4:5", ";;;;;;;;;;;;;;;;;;;;;;:"), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(cArgs == 6);
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[0], "0"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[1], "1"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[2], "2"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[3], "3"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[4], "4"));
    RTTESTI_CHECK_RETV(!strcmp(papszArgs[5], "5"));
    RTTESTI_CHECK_RETV(!papszArgs[6]);
    RTGetOptArgvFree(papszArgs);

    /*
     * Tests from the list.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aBourneTests); i++)
    {
        papszArgs = NULL;
        cArgs = -1;
        int rc = RTGetOptArgvFromString(&papszArgs, &cArgs, g_aBourneTests[i].pszInput, g_aBourneTests[i].pszSeparators);
        if (rc == VINF_SUCCESS)
        {
            if (cArgs == g_aBourneTests[i].cArgs)
            {
                for (int iArg = 0; iArg < cArgs; iArg++)
                    if (strcmp(papszArgs[iArg], g_aBourneTests[i].apszArgs[iArg]) != 0)
                        RTTestIFailed("g_aBourneTests[%i]: argv[%i] differs: got '%s', expected '%s' (RTGetOptArgvFromString(,,'%s', '%s'))",
                                      i, iArg, papszArgs[iArg], g_aBourneTests[i].apszArgs[iArg],
                                      g_aBourneTests[i].pszInput, g_aBourneTests[i].pszSeparators);
                RTTESTI_CHECK_RETV(papszArgs[cArgs] == NULL);
            }
            else
                RTTestIFailed("g_aBourneTests[%i]: cArgs=%u, expected %u for RTGetOptArgvFromString(,,'%s', '%s')",
                              i, cArgs, g_aBourneTests[i].cArgs, g_aBourneTests[i].pszInput, g_aBourneTests[i].pszSeparators);
            RTGetOptArgvFree(papszArgs);
        }
        else
            RTTestIFailed("g_aBourneTests[%i]: RTGetOptArgvFromString(,,'%s', '%s') -> %Rrc",
                          i, g_aBourneTests[i].pszInput, g_aBourneTests[i].pszSeparators, rc);
    }
}


int main()
{
    /*
     * Init RT+Test.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTGetOptArgv", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * The test.
     */
    tst1();
    tst2();
    tst3();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

