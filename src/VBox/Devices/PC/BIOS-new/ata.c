/*
 * Copyright (C) 2006-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 *  ROM BIOS for use with Bochs/Plex86/QEMU emulation environment
 *
 *  Copyright (C) 2002  MandrakeSoft S.A.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */


#include <stdint.h>
#include <stdarg.h>
#include "inlines.h"
#include "biosint.h"
#include "ebda.h"
#include "ata.h"

#if DEBUG_ATA
#  define BX_DEBUG_ATA(...) BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_ATA(...)
#endif


// ---------------------------------------------------------------------------
// Start of ATA/ATAPI Driver
// ---------------------------------------------------------------------------

void insw_discard(unsigned nwords, unsigned port);
#pragma aux insw_discard =  \
    ".286"                  \
    "again:"                \
    "in ax,dx"              \
    "loop again"            \
    parm [cx] [dx] modify exact [cx ax] nomemory;

void insd_discard(unsigned ndwords, unsigned port);
#pragma aux insd_discard =  \
    ".386"                  \
    "push eax"              \
    "again:"                \
    "in eax,dx"             \
    "loop again"            \
    "pop eax"               \
    parm [cx] [dx] modify exact [cx] nomemory;

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : initialization
// ---------------------------------------------------------------------------
void BIOSCALL ata_init(void)
{
    uint8_t         channel, device;
    bio_dsk_t __far *bios_dsk;
    
    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;
    
    // Channels info init.
    for (channel=0; channel<BX_MAX_ATA_INTERFACES; channel++) {
        bios_dsk->channels[channel].iface   = ATA_IFACE_NONE;
        bios_dsk->channels[channel].iobase1 = 0x0;
        bios_dsk->channels[channel].iobase2 = 0x0;
        bios_dsk->channels[channel].irq     = 0;
    }
    
    // Devices info init.
    for (device=0; device<BX_MAX_ATA_DEVICES; device++) {
        bios_dsk->devices[device].type        = ATA_TYPE_NONE;
        bios_dsk->devices[device].device      = ATA_DEVICE_NONE;
        bios_dsk->devices[device].removable   = 0;
        bios_dsk->devices[device].lock        = 0;
        bios_dsk->devices[device].mode        = ATA_MODE_NONE;
        bios_dsk->devices[device].blksize     = 0x200;
        bios_dsk->devices[device].translation = ATA_TRANSLATION_NONE;
        bios_dsk->devices[device].lchs.heads     = 0;
        bios_dsk->devices[device].lchs.cylinders = 0;
        bios_dsk->devices[device].lchs.spt       = 0;
        bios_dsk->devices[device].pchs.heads     = 0;
        bios_dsk->devices[device].pchs.cylinders = 0;
        bios_dsk->devices[device].pchs.spt       = 0;
        bios_dsk->devices[device].sectors     = 0;
    }
    
    // hdidmap  and cdidmap init.
    for (device=0; device<BX_MAX_STORAGE_DEVICES; device++) {
        bios_dsk->hdidmap[device] = BX_MAX_STORAGE_DEVICES;
        bios_dsk->cdidmap[device] = BX_MAX_STORAGE_DEVICES;
    }
    
    bios_dsk->hdcount = 0;
    bios_dsk->cdcount = 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : software reset
// ---------------------------------------------------------------------------
// ATA-3
// 8.2.1 Software reset - Device 0

void   ata_reset(uint16_t device)
{
    uint16_t        iobase1, iobase2;
    uint8_t         channel, slave, sn, sc;
    uint16_t        max;
    uint16_t        pdelay;
    bio_dsk_t __far *bios_dsk;
    
    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;
    channel = device / 2;
    slave = device % 2;
    
    iobase1 = bios_dsk->channels[channel].iobase1;
    iobase2 = bios_dsk->channels[channel].iobase2;

    // Reset

    // 8.2.1 (a) -- set SRST in DC
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN | ATA_CB_DC_SRST);

    // 8.2.1 (b) -- wait for BSY
    max=0xff;
    while(--max>0) {
        uint8_t status = inb(iobase1+ATA_CB_STAT);
        if ((status & ATA_CB_STAT_BSY) != 0)
            break;
    }

    // 8.2.1 (f) -- clear SRST
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);

    if (bios_dsk->devices[device].type != ATA_TYPE_NONE) {
        // 8.2.1 (g) -- check for sc==sn==0x01
        // select device
        outb(iobase1+ATA_CB_DH, slave?ATA_CB_DH_DEV1:ATA_CB_DH_DEV0);
        sc = inb(iobase1+ATA_CB_SC);
        sn = inb(iobase1+ATA_CB_SN);

        if ( (sc==0x01) && (sn==0x01) ) {
            // 8.2.1 (h) -- wait for not BSY
            max=0xffff; /* The ATA specification says that the drive may be busy for up to 30 seconds. */
            while(--max>0) {
                uint8_t status = inb(iobase1+ATA_CB_STAT);
                if ((status & ATA_CB_STAT_BSY) == 0)
                    break;
                pdelay=0xffff;
                while (--pdelay>0) {
                    /* nothing */
                }
            }
        }
    }

    // 8.2.1 (i) -- wait for DRDY
    max = 0x10; /* Speed up for virtual drives. Disks are immediately ready, CDs never */
    while(--max>0) {
        uint8_t status = inb(iobase1+ATA_CB_STAT);
        if ((status & ATA_CB_STAT_RDY) != 0) 
            break;
    }
    
    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-in command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : BUSY bit set
      // 2 : read error
      // 3 : expected DRQ=1
      // 4 : no sectors left to read/verify
      // 5 : more sectors to read/verify
      // 6 : no sectors left to write
      // 7 : more sectors to write
uint16_t ata_cmd_data_in(bio_dsk_t __far *bios_dsk, uint16_t command, uint16_t count)
{
    uint32_t        lba;
    uint16_t        iobase1, iobase2, blksize, mult_blk_cnt;
    uint16_t        cylinder;
    uint16_t        head;
    uint16_t        sector;
    uint16_t        device;
    uint8_t         channel, slave;
    uint8_t         status, mode;
    char __far      *buffer;

    device  = bios_dsk->drqp.dev_id;
    channel = device / 2;
    slave   = device % 2;

    iobase1 = bios_dsk->channels[channel].iobase1;
    iobase2 = bios_dsk->channels[channel].iobase2;
    mode    = bios_dsk->devices[device].mode;
    blksize = bios_dsk->devices[device].blksize;
    if (blksize == 0) {   /* If transfer size is exactly 64K */
        if (mode == ATA_MODE_PIO32)
            blksize = 0x4000;
        else
            blksize = 0x8000;
    } else {
        if (mode == ATA_MODE_PIO32)
            blksize >>= 2;
        else
            blksize >>= 1;
    }
    
    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
    {
        BX_DEBUG_ATA("%s: disk busy\n", __func__);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 1;
    }

    lba      = bios_dsk->drqp.lba;
    buffer   = bios_dsk->drqp.buffer;
    sector   = bios_dsk->drqp.sector;
    cylinder = bios_dsk->drqp.cylinder;
    head     = bios_dsk->drqp.head;

    // sector will be 0 only on lba access. Convert to lba-chs
    if (sector == 0) {
        if (lba + count >= 268435456)
        {
            sector = (lba & 0xff000000L) >> 24;
            cylinder = 0; /* The parameter lba is just a 32 bit value. */
            outb(iobase1 + ATA_CB_SC, (count & 0xff00) >> 8);
            outb(iobase1 + ATA_CB_SN, sector);
            outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
            outb(iobase1 + ATA_CB_CH, cylinder >> 8);
            /* Leave the bottom 24 bits as is, they are treated correctly by the
            * LBA28 code path. */
            lba &= 0xffffff;
        }
        sector = (uint16_t) (lba & 0x000000ffL);
        lba >>= 8;
        cylinder = (uint16_t) (lba & 0x0000ffffL);
        lba >>= 16;
        head = ((uint16_t) (lba & 0x0000000fL)) | 0x40;
    }
    
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    outb(iobase1 + ATA_CB_FR, 0x00);
    outb(iobase1 + ATA_CB_SC, count);
    outb(iobase1 + ATA_CB_SN, sector);
    outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
    outb(iobase1 + ATA_CB_CH, cylinder >> 8);
    outb(iobase1 + ATA_CB_DH, (slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | (uint8_t) head );
    outb(iobase1 + ATA_CB_CMD, command);
    
    if (command == ATA_CMD_READ_MULTIPLE || command == ATA_CMD_READ_MULTIPLE_EXT) {
        mult_blk_cnt = count;
        count = 1;
    } else {
        mult_blk_cnt = 1;
    }
    
    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) )
            break;
    }
    
    if (status & ATA_CB_STAT_ERR) {
        BX_DEBUG_ATA("%s: read error\n", __func__);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("%s: DRQ not set (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 3;
    }
    
    // FIXME : move seg/off translation here
    
    int_enable(); // enable higher priority interrupts
    
    while (1) {
    
        // adjust if there will be an overrun. 2K max sector size
        if (FP_OFF(buffer) >= 0xF800)
            buffer = MK_FP(FP_SEG(buffer) + 0x80, FP_OFF(buffer) - 0x800);
        
        if (mode == ATA_MODE_PIO32) {
            buffer = rep_insd(buffer, blksize, iobase1);
        } else {
            buffer = rep_insw(buffer, blksize, iobase1);
        }
        bios_dsk->drqp.trsfsectors += mult_blk_cnt;
        count--;
        while (1) {
            status = inb(iobase1 + ATA_CB_STAT);
            if ( !(status & ATA_CB_STAT_BSY) )
                break;
        }
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != ATA_CB_STAT_RDY ) {
                BX_DEBUG_ATA("%s: no sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 4;
            }
            break;
        }
        else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("%s: more sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 5;
            }
            continue;
        }
    }
    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : device detection
// ---------------------------------------------------------------------------

void BIOSCALL ata_detect(void)
{
    uint16_t        ebda_seg = read_word(0x0040,0x000E);
    uint8_t         hdcount, cdcount, device, type;
    uint8_t         buffer[0x0200];
    bio_dsk_t __far *bios_dsk;
    
    bios_dsk = ebda_seg :> &EbdaData->bdisk;

#if BX_MAX_ATA_INTERFACES > 0
    bios_dsk->channels[0].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[0].iobase1 = 0x1f0;
    bios_dsk->channels[0].iobase2 = 0x3f0;
    bios_dsk->channels[0].irq     = 14;
#endif
#if BX_MAX_ATA_INTERFACES > 1
    bios_dsk->channels[1].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[1].iobase1 = 0x170;
    bios_dsk->channels[1].iobase2 = 0x370;
    bios_dsk->channels[1].irq     = 15;
#endif
#if 0   //@todo - temporarily removed to avoid conflict with AHCI
#if BX_MAX_ATA_INTERFACES > 2
    bios_dsk->channels[2].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[2].iobase1 = 0x1e8;
    bios_dsk->channels[2].iobase2 = 0x3e0;
    bios_dsk->channels[2].irq     = 12;
#endif
#if BX_MAX_ATA_INTERFACES > 3
    bios_dsk->channels[3].iface   = ATA_IFACE_ISA;
    bios_dsk->channels[3].iobase1 = 0x168;
    bios_dsk->channels[3].iobase2 = 0x360;
    bios_dsk->channels[3].irq     = 11;
#endif
#endif
#if BX_MAX_ATA_INTERFACES > 4
#error Please fill the ATA interface informations
#endif

    // Device detection
    hdcount = cdcount = 0;

    for (device = 0; device < BX_MAX_ATA_DEVICES; device++) {
        uint16_t    iobase1, iobase2;
        uint8_t     channel, slave;
        uint8_t     sc, sn, cl, ch, st;
        
        channel = device / 2;
        slave = device % 2;
        
        iobase1 = bios_dsk->channels[channel].iobase1;
        iobase2 = bios_dsk->channels[channel].iobase2;
        
        // Disable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
        
        // Look for device
        outb(iobase1+ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
        outb(iobase1+ATA_CB_SC, 0x55);
        outb(iobase1+ATA_CB_SN, 0xaa);
        outb(iobase1+ATA_CB_SC, 0xaa);
        outb(iobase1+ATA_CB_SN, 0x55);
        outb(iobase1+ATA_CB_SC, 0x55);
        outb(iobase1+ATA_CB_SN, 0xaa);
        
        // If we found something
        sc = inb(iobase1+ATA_CB_SC);
        sn = inb(iobase1+ATA_CB_SN);

        if ( (sc == 0x55) && (sn == 0xaa) ) {
            bios_dsk->devices[device].type = ATA_TYPE_UNKNOWN;

            // reset the channel
            ata_reset(device);

            // check for ATA or ATAPI
            outb(iobase1+ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
            sc = inb(iobase1+ATA_CB_SC);
            sn = inb(iobase1+ATA_CB_SN);
            if ((sc==0x01) && (sn==0x01)) {
                cl = inb(iobase1+ATA_CB_CL);
                ch = inb(iobase1+ATA_CB_CH);
                st = inb(iobase1+ATA_CB_STAT);

                if ((cl==0x14) && (ch==0xeb)) {
                    bios_dsk->devices[device].type = ATA_TYPE_ATAPI;
                } else if ((cl==0x00) && (ch==0x00) && (st!=0x00)) {
                    bios_dsk->devices[device].type = ATA_TYPE_ATA;
                } else if ((cl==0xff) && (ch==0xff)) {
                    bios_dsk->devices[device].type = ATA_TYPE_NONE;
                }
            }
        }

        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);

        type = bios_dsk->devices[device].type;

        // Now we send a IDENTIFY command to ATA device
        if (type == ATA_TYPE_ATA) {
            uint32_t    sectors;
            uint16_t    cylinders, heads, spt, blksize;
            uint16_t    lcylinders, lheads, lspt;
            uint8_t     chsgeo_base;
            uint8_t     removable, mode;

            //Temporary values to do the transfer
            bios_dsk->devices[device].device = ATA_DEVICE_HD;
            bios_dsk->devices[device].mode   = ATA_MODE_PIO16;
            bios_dsk->drqp.buffer = buffer;
            bios_dsk->drqp.dev_id = device;

            if (ata_cmd_data_in(bios_dsk, ATA_CMD_IDENTIFY_DEVICE, 1) !=0 )
                BX_PANIC("ata-detect: Failed to detect ATA device\n");

            removable = (*(buffer+0) & 0x80) ? 1 : 0;
            mode      = *(buffer+96) ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
            blksize   = 512;  /* There is no sector size field any more. */

            cylinders = *(uint16_t *)(buffer+(1*2)); // word 1
            heads     = *(uint16_t *)(buffer+(3*2)); // word 3
            spt       = *(uint16_t *)(buffer+(6*2)); // word 6

            sectors   = *(uint32_t *)(buffer+(60*2)); // word 60 and word 61
            /** @todo update sectors to be a 64 bit number (also lba...). */
            if (sectors == 268435455)
                sectors = *(uint32_t *)(buffer+(100*2)); // words 100 to 103 (someday)
            switch (device)
            {
            case 0:
                chsgeo_base = 0x1e;
                break;
            case 1:
                chsgeo_base = 0x26;
                break;
            case 2:
                chsgeo_base = 0x67;
                break;
            case 3:
                chsgeo_base = 0x70;
                break;
            case 4:
                chsgeo_base = 0x40;
                break;
            case 5:
                chsgeo_base = 0x48;
                break;
            case 6:
                chsgeo_base = 0x50;
                break;
            case 7:
                chsgeo_base = 0x58;
                break;
            default:
                chsgeo_base = 0;
            }
            if (chsgeo_base != 0)
            {
                lcylinders = inb_cmos(chsgeo_base) + (inb_cmos(chsgeo_base+1) << 8);
                lheads = inb_cmos(chsgeo_base+2);
                lspt = inb_cmos(chsgeo_base+7);
            }
            else
            {
                lcylinders = 0;
                lheads = 0;
                lspt = 0;
            }
            BX_INFO("ata%d-%d: PCHS=%u/%d/%d LCHS=%u/%u/%u\n", channel, slave,
                    cylinders,heads, spt, lcylinders, lheads, lspt);

            bios_dsk->devices[device].device         = ATA_DEVICE_HD;
            bios_dsk->devices[device].removable      = removable;
            bios_dsk->devices[device].mode           = mode;
            bios_dsk->devices[device].blksize        = blksize;
            bios_dsk->devices[device].pchs.heads     = heads;
            bios_dsk->devices[device].pchs.cylinders = cylinders;
            bios_dsk->devices[device].pchs.spt       = spt;
            bios_dsk->devices[device].sectors        = sectors;
            bios_dsk->devices[device].lchs.heads     = lheads;
            bios_dsk->devices[device].lchs.cylinders = lcylinders;
            bios_dsk->devices[device].lchs.spt       = lspt;
            if (device < 2)
            {
                uint8_t         sum, i;
                fdpt_t __far    *fdpt;

                if (device == 0)
                    fdpt = ebda_seg :> &EbdaData->fdpt0;
                else
                    fdpt = ebda_seg :> &EbdaData->fdpt1;

                /* Update the DPT for drive 0/1 pointed to by Int41/46. This used
                 * to be done at POST time with lots of ugly assembler code, which
                 * isn't worth the effort of converting from AMI to Award CMOS
                 * format. Just do it here. */
                fdpt->lcyl  = lcylinders;
                fdpt->lhead = lheads;
                fdpt->sig   = 0xa0;
                fdpt->spt   = spt;
                fdpt->cyl   = cylinders;
                fdpt->head  = heads;
                fdpt->lspt  = spt;
                sum = 0;
                for (i = 0; i < 0xf; i++)
                    sum += *((uint8_t __far *)fdpt + i);
                sum = -sum;
                fdpt->csum = sum;
            }

            // fill hdidmap
            bios_dsk->hdidmap[hdcount] = device;
            hdcount++;
        }

        // Now we send an IDENTIFY command to ATAPI device
        if (type == ATA_TYPE_ATAPI) {
            uint8_t     type, removable, mode;
            uint16_t    blksize;

            // Temporary values to do the transfer
            bios_dsk->devices[device].device = ATA_DEVICE_CDROM;
            bios_dsk->devices[device].mode   = ATA_MODE_PIO16;
            bios_dsk->drqp.buffer = buffer;
            bios_dsk->drqp.dev_id = device;

            if (ata_cmd_data_in(bios_dsk, ATA_CMD_IDENTIFY_DEVICE_PACKET, 1) != 0)
                BX_PANIC("ata-detect: Failed to detect ATAPI device\n");

            type      = *(buffer+1) & 0x1f;
            removable = (*(buffer+0) & 0x80) ? 1 : 0;
            mode      = *(buffer+96) ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
            blksize   = 2048;

            bios_dsk->devices[device].device    = type;
            bios_dsk->devices[device].removable = removable;
            bios_dsk->devices[device].mode      = mode;
            bios_dsk->devices[device].blksize   = blksize;

            // fill cdidmap
            bios_dsk->cdidmap[cdcount] = device;
            cdcount++;
        }

        {
            uint32_t    sizeinmb;
            uint16_t    ataversion;
            uint8_t     version, model[41];
            int         i;

            switch (type) {
            case ATA_TYPE_ATA:
                sizeinmb = bios_dsk->devices[device].sectors;
                sizeinmb >>= 11;
            case ATA_TYPE_ATAPI:
                // Read ATA/ATAPI version
                ataversion = ((uint16_t)(*(buffer+161))<<8) | *(buffer+160);
                for (version = 15; version > 0; version--) {
                    if ((ataversion & (1 << version)) !=0 )
                        break;
                }

                // Read model name
                for (i = 0; i < 20; i++ ) {
                    *(model+(i*2))   = *(buffer+(i*2)+54+1);
                    *(model+(i*2)+1) = *(buffer+(i*2)+54);
                }

                // Reformat
                *(model+40) = 0x00;
                for ( i = 39; i > 0; i-- ){
                    if (*(model+i) == 0x20)
                        *(model+i) = 0x00;
                    else 
                        break;
                }
                break;
            }

#ifdef VBOXz
            // we don't want any noisy output for now
#else /* !VBOX */
            switch (type) {
            int c;
            case ATA_TYPE_ATA:
                printf("ata%d %s: ", channel, slave ? " slave" : "master");
                i=0;
                while(c=*(model+i++))
                    printf("%c", c);
                printf(" ATA-%d Hard-Disk (%lu MBytes)\n", version, sizeinmb);
                break;
            case ATA_TYPE_ATAPI:
                printf("ata%d %s: ", channel, slave ? " slave" : "master");
                i=0;
                while(c=*(model+i++))
                    printf("%c", c);
                if (bios_dsk->devices[device].device == ATA_DEVICE_CDROM)
                    printf(" ATAPI-%d CD-ROM/DVD-ROM\n", version);
                else
                    printf(" ATAPI-%d Device\n", version);
                break;
            case ATA_TYPE_UNKNOWN:
                printf("ata%d %s: Unknown device\n", channel , slave ? " slave" : "master");
                break;
            }
#endif /* !VBOX */
        }
    }

    // Store the devices counts
    bios_dsk->hdcount = hdcount;
    bios_dsk->cdcount = cdcount;
    write_byte(0x40,0x75, hdcount);

#ifdef VBOX
      // we don't want any noisy output for now
#else /* !VBOX */
  printf("\n");
#endif /* !VBOX */

    // FIXME : should use bios=cmos|auto|disable bits
    // FIXME : should know about translation bits
    // FIXME : move hard_drive_post here

}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-out command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : BUSY bit set
      // 2 : read error
      // 3 : expected DRQ=1
      // 4 : no sectors left to read/verify
      // 5 : more sectors to read/verify
      // 6 : no sectors left to write
      // 7 : more sectors to write
uint16_t ata_cmd_data_out(bio_dsk_t __far *bios_dsk, uint16_t command, uint16_t count)
{
    uint32_t        lba;
    char __far      *buffer;
    uint16_t        iobase1, iobase2, blksize;
    uint16_t        cylinder;
    uint16_t        head;
    uint16_t        sector;
    uint16_t        device;
    uint8_t         channel, slave;
    uint8_t         status, mode;
    
    device  = bios_dsk->drqp.dev_id;
    channel = device / 2;
    slave   = device % 2;
    
    iobase1 = bios_dsk->channels[channel].iobase1;
    iobase2 = bios_dsk->channels[channel].iobase2;
    mode    = bios_dsk->devices[device].mode;
    blksize = 0x200; // was = bios_dsk->devices[device].blksize;
    if (mode == ATA_MODE_PIO32)
        blksize >>= 2;
    else
        blksize >>= 1;
    
    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
    {
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 1;
    }

    lba      = bios_dsk->drqp.lba;
    buffer   = bios_dsk->drqp.buffer;
    sector   = bios_dsk->drqp.sector;
    cylinder = bios_dsk->drqp.cylinder;
    head     = bios_dsk->drqp.head;

    // sector will be 0 only on lba access. Convert to lba-chs
    if (sector == 0) {
        if (lba + count >= 268435456)
        {
            sector = (lba & 0xff000000L) >> 24;
            cylinder = 0; /* The parameter lba is just a 32 bit value. */
            outb(iobase1 + ATA_CB_SC, (count & 0xff00) >> 8);
            outb(iobase1 + ATA_CB_SN, sector);
            outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
            outb(iobase1 + ATA_CB_CH, cylinder >> 8);
            /* Leave the bottom 24 bits as is, they are treated correctly by the
            * LBA28 code path. */
            lba &= 0xffffff;
        }
        sector = (uint16_t) (lba & 0x000000ffL);
        lba >>= 8;
        cylinder = (uint16_t) (lba & 0x0000ffffL);
        lba >>= 16;
        head = ((uint16_t) (lba & 0x0000000fL)) | 0x40;
    }
    
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    outb(iobase1 + ATA_CB_FR, 0x00);
    outb(iobase1 + ATA_CB_SC, count);
    outb(iobase1 + ATA_CB_SN, sector);
    outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
    outb(iobase1 + ATA_CB_CH, cylinder >> 8);
    outb(iobase1 + ATA_CB_DH, (slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | (uint8_t) head );
    outb(iobase1 + ATA_CB_CMD, command);
    
    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) )
            break;
    }
    
    if (status & ATA_CB_STAT_ERR) {
        BX_DEBUG_ATA("%s: read error\n", __func__);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("%s: DRQ not set (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 3;
    }
    
    // FIXME : move seg/off translation here
    
    int_enable(); // enable higher priority interrupts
    
    while (1) {
    
        // adjust if there will be an overrun. 2K max sector size
        if (FP_OFF(buffer) >= 0xF800)
            buffer = MK_FP(FP_SEG(buffer) + 0x80, FP_OFF(buffer) - 0x800);
        
        if (mode == ATA_MODE_PIO32) {
            buffer = rep_outsd(buffer, blksize, iobase1);
        } else {
            buffer = rep_outsw(buffer, blksize, iobase1);
        }
        
        bios_dsk->drqp.trsfsectors++;
        count--;
        while (1) {
            status = inb(iobase1 + ATA_CB_STAT);
            if ( !(status & ATA_CB_STAT_BSY) )
                break;
        }
        if (count == 0) {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != ATA_CB_STAT_RDY ) {
                BX_DEBUG_ATA("%s: no sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 6;
            }
            break;
        }
        else {
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("%s: more sectors left (status %02x)\n", __func__, (unsigned) status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 7;
            }
            continue;
        }
    }
    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}


/**
 * Read sectors from an attached ATA device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the 
 *                      EBDA).
 */
int ata_read_sectors(bio_dsk_t __far *bios_dsk)
{
    uint16_t    n_sect;
    int         status;
    uint8_t     device_id;

    device_id = bios_dsk->drqp.dev_id;
    n_sect    = bios_dsk->drqp.nsect;

    if (bios_dsk->drqp.sector) {
        /* CHS addressing. */
        bios_dsk->devices[device_id].blksize = n_sect * 0x200;
        status = ata_cmd_data_in(bios_dsk, ATA_CMD_READ_MULTIPLE, n_sect);
        bios_dsk->devices[device_id].blksize = 0x200;
    } else {
        /* LBA addressing. */
        if (bios_dsk->drqp.lba + n_sect >= 268435456)
            status = ata_cmd_data_in(bios_dsk, ATA_CMD_READ_SECTORS_EXT, n_sect);
        else {
            bios_dsk->devices[device_id].blksize = n_sect * 0x200;
            status = ata_cmd_data_in(bios_dsk, ATA_CMD_READ_MULTIPLE, n_sect);
            bios_dsk->devices[device_id].blksize = 0x200;
        }
    }
    return status;
}

/**
 * Write sectors to an attached ATA device.
 *
 * @returns status code.
 * @param   bios_dsk    Pointer to disk request packet (in the 
 *                      EBDA).
 */
int ata_write_sectors(bio_dsk_t __far *bios_dsk)
{
    uint16_t    n_sect;

    n_sect = bios_dsk->drqp.nsect;

    if (bios_dsk->drqp.sector) {
        /* CHS addressing. */
        return ata_cmd_data_out(bios_dsk, ATA_CMD_WRITE_SECTORS, n_sect);
    } else {
        /* LBA addressing. */
        if (bios_dsk->drqp.lba + n_sect >= 268435456)
            return ata_cmd_data_out(bios_dsk, ATA_CMD_WRITE_SECTORS_EXT, n_sect);
        else
            return ata_cmd_data_out(bios_dsk, ATA_CMD_WRITE_SECTORS, n_sect);
    }
}


// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a packet command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : error in parameters
      // 2 : BUSY bit set
      // 3 : error
      // 4 : not ready
uint16_t ata_cmd_packet(uint16_t device, uint8_t cmdlen, char __far *cmdbuf, 
                        uint16_t header, uint32_t length, uint8_t inout, char __far *buffer)
{
    uint16_t        iobase1, iobase2;
    uint16_t        lcount, lbefore, lafter, count;
    uint8_t         channel, slave;
    uint8_t         status, mode, lmode;
    uint32_t        transfer;
    bio_dsk_t __far *bios_dsk;

    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;

    channel = device / 2;
    slave = device % 2;

    // Data out is not supported yet
    if (inout == ATA_DATA_OUT) {
        BX_INFO("%s: DATA_OUT not supported yet\n", __func__);
        return 1;
    }
    
    // The header length must be even
    if (header & 1) {
        BX_DEBUG_ATA("%s: header must be even (%04x)\n", __func__, header);
        return 1;
    }
    
    iobase1  = bios_dsk->channels[channel].iobase1;
    iobase2  = bios_dsk->channels[channel].iobase2;
    mode     = bios_dsk->devices[device].mode;
    transfer = 0L;
    
    if (cmdlen < 12)
        cmdlen = 12;
    if (cmdlen > 12)
        cmdlen = 16;
    cmdlen >>= 1;
    
    // Reset count of transferred data
    // @todo: clear in calling code?
    bios_dsk->drqp.trsfsectors = 0;
    bios_dsk->drqp.trsfbytes   = 0;
    
    status = inb(iobase1 + ATA_CB_STAT);
    if (status & ATA_CB_STAT_BSY)
        return 2;
    
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    // outb(iobase1 + ATA_CB_FR, 0x00);
    // outb(iobase1 + ATA_CB_SC, 0x00);
    // outb(iobase1 + ATA_CB_SN, 0x00);
    outb(iobase1 + ATA_CB_CL, 0xfff0 & 0x00ff);
    outb(iobase1 + ATA_CB_CH, 0xfff0 >> 8);
    outb(iobase1 + ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
    outb(iobase1 + ATA_CB_CMD, ATA_CMD_PACKET);
    
    // Device should ok to receive command
    while (1) {
        status = inb(iobase1 + ATA_CB_STAT);
        if ( !(status & ATA_CB_STAT_BSY) ) break;
    }
    
    if (status & ATA_CB_STAT_ERR) {
        BX_DEBUG_ATA("%s: error, status is %02x\n", __func__, status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 3;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("%s: DRQ not set (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 4;
    }
    
    int_enable(); // enable higher priority interrupts
    
    // Normalize address
    BX_DEBUG_ATA("acp1 buffer ptr: %04x:%04x wlen %04x\n", FP_SEG(cmdbuf), FP_OFF(cmdbuf), cmdlen);
    cmdbuf = MK_FP(FP_SEG(cmdbuf) + FP_OFF(cmdbuf) / 16 , FP_OFF(cmdbuf) % 16);
    //  cmdseg += (cmdoff / 16);
    //  cmdoff %= 16;
    
    // Send command to device
    rep_outsw(cmdbuf, cmdlen, iobase1);
    
    if (inout == ATA_DATA_NO) {
        status = inb(iobase1 + ATA_CB_STAT);
    }
    else {
        while (1) {
        
            while (1) {
                status = inb(iobase1 + ATA_CB_STAT);
                if ( !(status & ATA_CB_STAT_BSY) ) 
                    break;
            }
            
            // Check if command completed
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_DRQ) ) ==0 )
                break;
            
            if (status & ATA_CB_STAT_ERR) {
                BX_DEBUG_ATA("%s: error (status %02x)\n", __func__, status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 3;
            }
            
            // Device must be ready to send data
            if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
              != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
                BX_DEBUG_ATA("%s: not ready (status %02x)\n", __func__, status);
                // Enable interrupts
                outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
                return 4;
            }
            
            // Normalize address
            BX_DEBUG_ATA("acp2 buffer ptr: %04x:%04x\n", FP_SEG(buffer), FP_OFF(buffer));
            buffer = MK_FP(FP_SEG(buffer) + FP_OFF(buffer) / 16 , FP_OFF(buffer) % 16);
            //      bufseg += (bufoff / 16);
            //      bufoff %= 16;
            
            // Get the byte count
            lcount =  ((uint16_t)(inb(iobase1 + ATA_CB_CH))<<8)+inb(iobase1 + ATA_CB_CL);
            
            // adjust to read what we want
            if (header>lcount) {
                lbefore = lcount;
                header -= lcount;
                lcount  = 0;
            }
            else {
                lbefore = header;
                header  = 0;
                lcount -= lbefore;
            }
            
            if (lcount>length) {
                lafter = lcount - length;
                lcount = length;
                length = 0;
            }
            else {
                lafter  = 0;
                length -= lcount;
            }
            
            // Save byte count
            count = lcount;
            
            BX_DEBUG_ATA("Trying to read %04x bytes (%04x %04x %04x) ",lbefore+lcount+lafter,lbefore,lcount,lafter);
            BX_DEBUG_ATA("to 0x%04x:0x%04x\n",FP_SEG(buffer),FP_OFF(buffer));
            
            // If counts not dividable by 4, use 16bits mode
            lmode = mode;
            if (lbefore & 0x03)
                lmode = ATA_MODE_PIO16;
            if (lcount  & 0x03)
                lmode = ATA_MODE_PIO16;
            if (lafter  & 0x03)
                lmode = ATA_MODE_PIO16;
            
            // adds an extra byte if count are odd. before is always even
            if (lcount & 0x01) {
                lcount += 1;
                if ((lafter > 0) && (lafter & 0x01)) {
                    lafter -= 1;
                }
            }
            
            if (lmode == ATA_MODE_PIO32) {
                lcount  >>= 2;
                lbefore >>= 2;
                lafter  >>= 2;
            }
            else {
                lcount  >>= 1;
                lbefore >>= 1;
                lafter  >>= 1;
            }
            
            if (lmode == ATA_MODE_PIO32) {
                if (lbefore)
                    insd_discard(lbefore, iobase1);
                rep_insd(buffer, lcount, iobase1);
                if (lafter)
                    insd_discard(lafter, iobase1);
            } else {
                if (lbefore)
                    insw_discard(lbefore, iobase1);
                rep_insw(buffer, lcount, iobase1);
                if (lafter)
                    insw_discard(lafter, iobase1);
            }
            
            
            // Compute new buffer address
            buffer += count;
            
            // Save transferred bytes count
            transfer += count;
            bios_dsk->drqp.trsfbytes = transfer;
        }
    }
    
    // Final check, device must be ready
    if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) )
      != ATA_CB_STAT_RDY ) {
        BX_DEBUG_ATA("%s: not ready (status %02x)\n", __func__, (unsigned) status);
        // Enable interrupts
        outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
        return 4;
    }
    
    // Enable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
    return 0;
}


// ---------------------------------------------------------------------------
// End of ATA/ATAPI Driver
// ---------------------------------------------------------------------------

uint16_t atapi_is_cdrom(uint8_t device)
{
    bio_dsk_t __far *bios_dsk;
    
    bios_dsk = read_word(0x0040, 0x000E) :> &EbdaData->bdisk;
    
    if (device >= BX_MAX_ATA_DEVICES)
        return 0;
    
    if (bios_dsk->devices[device].type != ATA_TYPE_ATAPI)
        return 0;
    
    if (bios_dsk->devices[device].device != ATA_DEVICE_CDROM)
        return 0;
    
    return 1;
}

// ---------------------------------------------------------------------------
// End of ATA/ATAPI generic functions
// ---------------------------------------------------------------------------
