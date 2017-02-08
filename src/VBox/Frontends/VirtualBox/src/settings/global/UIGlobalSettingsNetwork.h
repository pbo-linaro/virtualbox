/* $Id$ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsNetwork class declaration.
 */

/*
 * Copyright (C) 2009-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___UIGlobalSettingsNetwork_h___
#define ___UIGlobalSettingsNetwork_h___

/* GUI includes: */
#include "UISettingsPage.h"
#include "UIGlobalSettingsNetwork.gen.h"
#include "UIPortForwardingTable.h"

/* Forward declarations: */
class UIItemNetworkNAT;
class UIItemNetworkHost;


/** Global settings: Network page: NAT network cache structure. */
struct UIDataSettingsGlobalNetworkNAT
{
    /** Holds whether this network enabled. */
    bool m_fEnabled;
    /** Holds network name. */
    QString m_strName;
    /** Holds new network name. */
    QString m_strNewName;
    /** Holds network CIDR. */
    QString m_strCIDR;
    /** Holds whether this network supports DHCP. */
    bool m_fSupportsDHCP;
    /** Holds whether this network supports IPv6. */
    bool m_fSupportsIPv6;
    /** Holds whether this network advertised as default IPv6 route. */
    bool m_fAdvertiseDefaultIPv6Route;
    /** Holds IPv4 port forwarding rules. */
    UIPortForwardingDataList m_ipv4rules;
    /** Holds IPv6 port forwarding rules. */
    UIPortForwardingDataList m_ipv6rules;

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalNetworkNAT &other) const
    {
        return true
               && (m_fEnabled == other.m_fEnabled)
               && (m_strName == other.m_strName)
               && (m_strNewName == other.m_strNewName)
               && (m_strCIDR == other.m_strCIDR)
               && (m_fSupportsDHCP == other.m_fSupportsDHCP)
               && (m_fSupportsIPv6 == other.m_fSupportsIPv6)
               && (m_fAdvertiseDefaultIPv6Route == other.m_fAdvertiseDefaultIPv6Route)
               && (m_ipv4rules == other.m_ipv4rules)
               && (m_ipv6rules == other.m_ipv6rules)
               ;
    }
};


/** Global settings: Network page: Host interface cache structure. */
struct UIDataSettingsGlobalNetworkHostInterface
{
    /** Holds host interface name. */
    QString m_strName;
    /** Holds whether DHCP client enabled. */
    bool m_fDhcpClientEnabled;
    /** Holds IPv4 interface address. */
    QString m_strInterfaceAddress;
    /** Holds IPv4 interface mask. */
    QString m_strInterfaceMask;
    /** Holds whether IPv6 protocol supported. */
    bool m_fIpv6Supported;
    /** Holds IPv6 interface address. */
    QString m_strInterfaceAddress6;
    /** Holds IPv6 interface mask length. */
    QString m_strInterfaceMaskLength6;

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalNetworkHostInterface &other) const
    {
        return true
               && (m_strName == other.m_strName)
               && (m_fDhcpClientEnabled == other.m_fDhcpClientEnabled)
               && (m_strInterfaceAddress == other.m_strInterfaceAddress)
               && (m_strInterfaceMask == other.m_strInterfaceMask)
               && (m_fIpv6Supported == other.m_fIpv6Supported)
               && (m_strInterfaceAddress6 == other.m_strInterfaceAddress6)
               && (m_strInterfaceMaskLength6 == other.m_strInterfaceMaskLength6)
               ;
    }
};


/** Global settings: Network page: DHCP server cache structure. */
struct UIDataSettingsGlobalNetworkDHCPServer
{
    /** Holds whether DHCP server enabled. */
    bool m_fDhcpServerEnabled;
    /** Holds DHCP server address. */
    QString m_strDhcpServerAddress;
    /** Holds DHCP server mask. */
    QString m_strDhcpServerMask;
    /** Holds DHCP server lower address. */
    QString m_strDhcpLowerAddress;
    /** Holds DHCP server upper address. */
    QString m_strDhcpUpperAddress;

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalNetworkDHCPServer &other) const
    {
        return true
               && (m_fDhcpServerEnabled == other.m_fDhcpServerEnabled)
               && (m_strDhcpServerAddress == other.m_strDhcpServerAddress)
               && (m_strDhcpServerMask == other.m_strDhcpServerMask)
               && (m_strDhcpLowerAddress == other.m_strDhcpLowerAddress)
               && (m_strDhcpUpperAddress == other.m_strDhcpUpperAddress)
               ;
    }
};


/** Global settings: Network page: Host network cache structure. */
struct UIDataSettingsGlobalNetworkHost
{
    /** Holds the host interface data. */
    UIDataSettingsGlobalNetworkHostInterface m_interface;
    /** Holds the DHCP server data. */
    UIDataSettingsGlobalNetworkDHCPServer m_dhcpserver;

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalNetworkHost &other) const
    {
        return true
               && (m_interface == other.m_interface)
               && (m_dhcpserver == other.m_dhcpserver)
               ;
    }
};


/** Global settings: Network page cache structure. */
struct UISettingsCacheGlobalNetwork
{
    /** Holds the NAT network data. */
    QList<UIDataSettingsGlobalNetworkNAT> m_networksNAT;
    /** Holds the host network data. */
    QList<UIDataSettingsGlobalNetworkHost> m_networksHost;
};


/** Global settings: Network page. */
class UIGlobalSettingsNetwork : public UISettingsPageGlobal, public Ui::UIGlobalSettingsNetwork
{
    Q_OBJECT;

public:

    /* Constructor: */
    UIGlobalSettingsNetwork();

protected:

    /* API:
     * Load data to cache from corresponding external object(s),
     * this task COULD be performed in other than GUI thread: */
    void loadToCacheFrom(QVariant &data);
    /* API:
     * Load data to corresponding widgets from cache,
     * this task SHOULD be performed in GUI thread only: */
    void getFromCache();

    /* API:
     * Save data from corresponding widgets to cache,
     * this task SHOULD be performed in GUI thread only: */
    void putToCache();
    /* API:
     * Save data from cache to corresponding external object(s),
     * this task COULD be performed in other than GUI thread: */
    void saveFromCacheTo(QVariant &data);

    /* API: Validation stuff: */
    bool validate(QList<UIValidationMessage> &messages);

    /* API: Navigation stuff: */
    void setOrderAfter(QWidget *pWidget);

    /* API: Translation stuff: */
    void retranslateUi();

private slots:

    /* Handlers: NAT network stuff: */
    void sltAddNetworkNAT();
    void sltDelNetworkNAT();
    void sltEditNetworkNAT();
    void sltHandleItemChangeNetworkNAT(QTreeWidgetItem *pChangedItem);
    void sltHandleCurrentItemChangeNetworkNAT();
    void sltShowContextMenuNetworkNAT(const QPoint &pos);

    /* Handlers: Host network stuff: */
    void sltAddNetworkHost();
    void sltDelNetworkHost();
    void sltEditNetworkHost();
    void sltHandleCurrentItemChangeNetworkHost();
    void sltShowContextMenuNetworkHost(const QPoint &pos);

private:

    /* Helpers: NAT network cache stuff: */
    UIDataSettingsGlobalNetworkNAT generateDataNetworkNAT(const CNATNetwork &network);
    void saveCacheItemNetworkNAT(const UIDataSettingsGlobalNetworkNAT &data);

    /* Helpers: NAT network tree stuff: */
    void createTreeItemNetworkNAT(const UIDataSettingsGlobalNetworkNAT &data, bool fChooseItem = false);
    void removeTreeItemNetworkNAT(UIItemNetworkNAT *pItem);

    /* Helpers: Host network cache stuff: */
    UIDataSettingsGlobalNetworkHost generateDataNetworkHost(const CHostNetworkInterface &iface);
    void saveCacheItemNetworkHost(const UIDataSettingsGlobalNetworkHost &data);

    /* Helpers: Host network tree stuff: */
    void createTreeItemNetworkHost(const UIDataSettingsGlobalNetworkHost &data, bool fChooseItem = false);
    void removeTreeItemNetworkHost(UIItemNetworkHost *pItem);

    /* Variables: NAT network actions: */
    QAction *m_pActionAddNetworkNAT;
    QAction *m_pActionDelNetworkNAT;
    QAction *m_pActionEditNetworkNAT;

    /* Variables: Host network actions: */
    QAction *m_pActionAddNetworkHost;
    QAction *m_pActionDelNetworkHost;
    QAction *m_pActionEditNetworkHost;

    /* Variable: Editness flag: */
    bool m_fChanged;

    /* Variable: Cache: */
    UISettingsCacheGlobalNetwork m_cache;
};

#endif /* !___UIGlobalSettingsNetwork_h___ */

