ScriptName SLID_MCM extends SKI_ConfigBase

; =============================================================================
; SLID Mod Configuration Menu
; =============================================================================
;
; Extends SkyUI's SKI_ConfigBase for MCM integration.
; All settings are stored in the DLL and persisted to SLID.ini.
; This script acts as a stateless UI layer — the DLL is the source of truth.

; --- Pages ---
; 0: Settings
; 1: Link
; 2: Presets
; 3: Sales Chest
; 4: Compatibility
; 5: Maintenance
; 6: About

; --- Option IDs (reset per page) ---
int _oidModEnabled
int _oidDebugLogging

; Settings page - Container Picker
int _oidIncludeUnlinkedContainers
int _oidIncludeSCIEContainers

; Sales page
int _oidSellPricePercent
int _oidSellBatchSize
int _oidSellIntervalHours
int _oidSellDefaults
int _oidVendorPricePercent
int _oidVendorBatchSize
int _oidVendorIntervalHours
int _oidVendorCost
int _oidWholesaleDefaults

; Maintenance page
int _oidSummonEnabled
int _oidShowWelcome
int _oidGrantPowers
int _oidResetAllData

; Link page
int _oidNetworkSelector
int _oidRunSort
int _oidRunSweep
int _oidDestroyLink

; Presets page - per-link export
int[] _oidExportLinks
string[] _exportLinkNames

; Compatibility page
int _oidSCIEIntegration

; Preset tracking
int[] _oidPresets
string[] _presetNames

; Containerlist tracking
int[] _oidContainerLists
string[] _containerListNames

; Vendor tracking
int[] _oidVendors
string[] _vendorInfoTexts

; State
string _selectedNetwork = ""
string[] _networkNames

; =============================================================================
; INITIALIZATION
; =============================================================================

event OnConfigInit()
    ModName = "SLID"
    CurrentVersion = 200  ; Bump to force OnVersionUpdate on existing saves
    InitializePages()
    InitializeArrays()
endEvent

event OnVersionUpdate(int a_version)
    ; Called by SkyUI when CurrentVersion increases - reinitialize for existing saves
    InitializePages()
    InitializeArrays()
endEvent

function InitializePages()
    Pages = new string[7]
    Pages[0] = "$SLID_PageSettings"
    Pages[1] = "$SLID_PageLink"
    Pages[2] = "$SLID_PagePresets"
    Pages[3] = "$SLID_PageSalesChest"
    Pages[4] = "$SLID_PageCompatibility"
    Pages[5] = "$SLID_PageMaintenance"
    Pages[6] = "$SLID_PageAbout"
endFunction

function InitializeArrays()
    _oidPresets = new int[10]
    _presetNames = new string[10]
    _oidContainerLists = new int[10]
    _containerListNames = new string[10]
    _oidVendors = new int[10]
    _vendorInfoTexts = new string[10]
    _oidExportLinks = new int[10]
    _exportLinkNames = new string[10]
endFunction

; =============================================================================
; PAGE RENDERING
; =============================================================================

event OnPageReset(string a_page)
    ; Always reinitialize pages to ensure updated keys are used (save may have stale values)
    InitializePages()

    ; Ensure arrays are initialized (covers saves from before preset arrays existed)
    if (!_oidVendors || _oidVendors.Length != 10 || !_oidPresets || _oidPresets.Length != 10 || !_oidContainerLists || _oidContainerLists.Length != 10 || !_oidExportLinks || _oidExportLinks.Length != 10)
        InitializeArrays()
    endif

    ResetOptionIDs()

    if (a_page == "$SLID_PageSettings")
        RenderSettingsPage()
    elseif (a_page == "$SLID_PageLink")
        RenderLinkPage()
    elseif (a_page == "$SLID_PageSalesChest")
        RenderSalesPage()
    elseif (a_page == "$SLID_PageCompatibility")
        RenderCompatibilityPage()
    elseif (a_page == "$SLID_PageMaintenance")
        RenderMaintenancePage()
    elseif (a_page == "$SLID_PagePresets")
        RenderPresetsPage()
    elseif (a_page == "$SLID_PageAbout")
        RenderAboutPage()
    else
        ; Default/splash page
        RenderSplashPage()
    endif
endEvent

function ResetOptionIDs()
    _oidModEnabled = -1
    _oidDebugLogging = -1
    _oidIncludeUnlinkedContainers = -1
    _oidIncludeSCIEContainers = -1
    _oidSellPricePercent = -1
    _oidSellBatchSize = -1
    _oidSellIntervalHours = -1
    _oidSellDefaults = -1
    _oidVendorPricePercent = -1
    _oidVendorBatchSize = -1
    _oidVendorIntervalHours = -1
    _oidVendorCost = -1
    _oidWholesaleDefaults = -1
    _oidSummonEnabled = -1
    _oidShowWelcome = -1
    _oidGrantPowers = -1
    _oidResetAllData = -1
    _oidNetworkSelector = -1
    _oidRunSort = -1
    _oidRunSweep = -1
    _oidDestroyLink = -1
    _oidSCIEIntegration = -1

    int i = 0
    while (i < 10)
        _oidPresets[i] = -1
        _presetNames[i] = ""
        _oidContainerLists[i] = -1
        _containerListNames[i] = ""
        _oidVendors[i] = -1
        _vendorInfoTexts[i] = ""
        _oidExportLinks[i] = -1
        _exportLinkNames[i] = ""
        i += 1
    endWhile
endFunction

; =============================================================================
; SPLASH PAGE (no page selected)
; =============================================================================

function RenderSplashPage()
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption("$SLID_SplashTitle")
    AddTextOption("$SLID_SplashSubtitle", "v" + SLID_Native.GetPluginVersion())
endFunction

; =============================================================================
; SETTINGS PAGE
; =============================================================================

function RenderSettingsPage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    AddHeaderOption("$SLID_HeaderContainerPicker")
    _oidIncludeUnlinkedContainers = AddToggleOption("$SLID_IncludeUnlinkedContainers", SLID_Native.GetIncludeUnlinkedContainers())

    int scieFlag = OPTION_FLAG_DISABLED
    if (SLID_Native.IsSCIEInstalled())
        scieFlag = OPTION_FLAG_NONE
    endif
    _oidIncludeSCIEContainers = AddToggleOption("$SLID_IncludeSCIEContainers", SLID_Native.GetIncludeSCIEContainers(), scieFlag)
endFunction

; =============================================================================
; LINK PAGE
; =============================================================================

function RenderLinkPage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    ; Refresh network list
    _networkNames = SLID_Native.GetNetworkNames()
    int networkCount = _networkNames.Length

    if (networkCount == 0)
        AddHeaderOption("$SLID_HeaderNoLinks")
        AddTextOption("$SLID_NoLinksHint", "")
        return
    endif

    ; Default to first network if none selected
    if (_selectedNetwork == "" && networkCount > 0)
        _selectedNetwork = _networkNames[0]
    endif

    ; Link Actions section with selector and action buttons
    AddHeaderOption("$SLID_HeaderLinkActions")
    _oidNetworkSelector = AddMenuOption("$SLID_SelectLink", _selectedNetwork)
    _oidRunSort = AddTextOption("$SLID_RunSort", "")
    _oidRunSweep = AddTextOption("$SLID_RunSweep", "")
    _oidDestroyLink = AddTextOption("$SLID_DestroyLink", "")

    ; Move to right column for container list
    SetCursorPosition(1)
    SetCursorFillMode(TOP_TO_BOTTOM)

    ; Container list (read-only, no remove buttons)
    string[] containers = SLID_Native.GetNetworkContainerNames(_selectedNetwork)
    int containerCount = containers.Length

    if (containerCount > 0)
        AddHeaderOption("$SLID_HeaderContainers")
        int i = 0
        while (i < containerCount)
            AddTextOption(containers[i], "", OPTION_FLAG_DISABLED)
            i += 1
        endWhile
    endif
endFunction

; =============================================================================
; SALES PAGE
; =============================================================================

function RenderSalesPage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    ; Left column: General Sales Vendor
    AddHeaderOption("$SLID_HeaderGeneralSalesVendor")
    _oidSellPricePercent = AddSliderOption("$SLID_SellPricePercent", SLID_Native.GetSellPricePercent() * 100.0, "{0}%")
    _oidSellBatchSize = AddSliderOption("$SLID_SellBatchSize", SLID_Native.GetSellBatchSize() as float, "{0}")
    _oidSellIntervalHours = AddSliderOption("$SLID_SellIntervalHours", SLID_Native.GetSellIntervalHours(), "{1} hrs")
    _oidSellDefaults = AddTextOption("$SLID_Defaults", "")

    AddEmptyOption()

    ; Wholesale Arrangements
    int vendorCount = SLID_Native.GetRegisteredVendorCount()
    AddHeaderOption("$SLID_HeaderWholesale")
    if (vendorCount > 0)
        string[] vendorNames = SLID_Native.GetRegisteredVendorNames()
        int i = 0
        while (i < vendorCount && i < 10)
            string storeName = SLID_Native.GetVendorStoreName(i)
            string categories = SLID_Native.GetVendorCategories(i)
            float bonus = SLID_Native.GetVendorBonusPercent(i)
            string lastVisit = SLID_Native.GetVendorLastVisit(i)

            ; Row shows store name with invested indicator
            string valueText = storeName
            if (bonus > 0.0)
                valueText = storeName + " ($SLID_VendorInvested)"
            endif

            ; Info text shows full details
            string infoText = vendorNames[i] + " - " + storeName + " | Buys: " + categories
            if (bonus > 0.0)
                infoText = infoText + " | Invested: +" + (bonus as int) + "% rate"
            endif
            infoText = infoText + " | Last visit: " + lastVisit
            _vendorInfoTexts[i] = infoText

            _oidVendors[i] = AddTextOption(vendorNames[i], valueText)
            i += 1
        endWhile
    else
        AddTextOption("$SLID_NoVendorsHint1", "", OPTION_FLAG_DISABLED)
        AddTextOption("$SLID_NoVendorsHint2", "", OPTION_FLAG_DISABLED)
        AddTextOption("$SLID_NoVendorsHint3", "", OPTION_FLAG_DISABLED)
    endif

    ; Right column: Wholesale Vendor Settings
    SetCursorPosition(1)
    SetCursorFillMode(TOP_TO_BOTTOM)

    AddHeaderOption("$SLID_HeaderWholesaleVendorSettings")
    _oidVendorPricePercent = AddSliderOption("$SLID_VendorPricePercent", SLID_Native.GetVendorPricePercent() * 100.0, "{0}%")
    _oidVendorBatchSize = AddSliderOption("$SLID_VendorBatchSize", SLID_Native.GetVendorBatchSize() as float, "{0}")
    _oidVendorIntervalHours = AddSliderOption("$SLID_VendorIntervalHours", SLID_Native.GetVendorIntervalHours(), "{1} hrs")
    _oidVendorCost = AddSliderOption("$SLID_VendorCost", SLID_Native.GetVendorCost() as float, "{0} gold")
    _oidWholesaleDefaults = AddTextOption("$SLID_Defaults", "")
endFunction

; =============================================================================
; COMPATIBILITY PAGE
; =============================================================================

function RenderCompatibilityPage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    ; SCIE section
    bool scieInstalled = SLID_Native.IsSCIEInstalled()
    AddHeaderOption("$SLID_HeaderSCIE")
    if (scieInstalled)
        AddTextOption("$SLID_SCIEName", "$SLID_Detected")
    else
        AddTextOption("$SLID_SCIEName", "$SLID_NotDetected", OPTION_FLAG_DISABLED)
    endif

    ; SCIE integration toggle
    int scieIntegrationFlag = OPTION_FLAG_DISABLED
    if (scieInstalled)
        scieIntegrationFlag = OPTION_FLAG_NONE
    endif

    _oidSCIEIntegration = AddToggleOption("$SLID_SCIEIntegration", SLID_Native.GetSCIEIntegration(), scieIntegrationFlag)

    AddEmptyOption()

    ; TCC section
    AddHeaderOption("$SLID_HeaderDetectedMods")
    bool tccInstalled = SLID_Native.IsTCCInstalled()
    if (tccInstalled)
        AddTextOption("$SLID_ModTCC", "$SLID_Detected")
        AddTextOption("$SLID_MuseumFilterAvailable", "", OPTION_FLAG_DISABLED)
    else
        AddTextOption("$SLID_ModTCC", "$SLID_NotDetected", OPTION_FLAG_DISABLED)
    endif
endFunction

; =============================================================================
; MAINTENANCE PAGE
; =============================================================================

function RenderMaintenancePage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    AddHeaderOption("$SLID_HeaderSettings")
    _oidModEnabled = AddToggleOption("$SLID_ModEnabled", SLID_Native.GetModEnabled())
    _oidDebugLogging = AddToggleOption("$SLID_DebugLogging", SLID_Native.GetDebugLogging())

    AddEmptyOption()

    AddHeaderOption("$SLID_HeaderPowers")
    _oidSummonEnabled = AddToggleOption("$SLID_SummonEnabled", SLID_Native.GetSummonEnabled())
    _oidGrantPowers = AddTextOption("$SLID_GrantPowers", "")

    AddEmptyOption()

    AddHeaderOption("$SLID_HeaderHelp")
    ; Show as "Show Welcome Guide" when already shown, toggle to re-enable
    _oidShowWelcome = AddToggleOption("$SLID_ShowWelcome", !SLID_Native.GetShownWelcomeTutorial())

    AddEmptyOption()

    AddHeaderOption("$SLID_HeaderDangerZone")
    _oidResetAllData = AddTextOption("$SLID_ResetAllData", "", OPTION_FLAG_NONE)
endFunction

; =============================================================================
; PRESETS PAGE
; =============================================================================

function RenderPresetsPage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    ; Fetch network names (used by Your Links section)
    _networkNames = SLID_Native.GetNetworkNames()
    int networkCount = _networkNames.Length

    ; --- Your Links section (export) ---
    AddHeaderOption("$SLID_HeaderYourLinks")
    if (networkCount == 0)
        AddTextOption("$SLID_ExportNoLinks", "", OPTION_FLAG_DISABLED)
        AddEmptyOption()
    else
        int li = 0
        while (li < networkCount && li < 10)
            _exportLinkNames[li] = _networkNames[li]
            _oidExportLinks[li] = AddTextOption("<font color='#AAAAAA'>" + _networkNames[li] + "</font>", "$SLID_Export")
            li += 1
        endWhile
        AddEmptyOption()
    endif

    int presetCount = SLID_Native.GetPresetCount()
    string[] presetNameList
    if (presetCount > 0)
        presetNameList = SLID_Native.GetPresetNames()
    endif

    ; User-generated presets section
    bool hasUserPresets = false
    if (presetCount > 0)
        int uCheck = 0
        while (uCheck < presetCount && uCheck < 10)
            if (SLID_Native.IsPresetUserGenerated(presetNameList[uCheck]))
                hasUserPresets = true
            endif
            uCheck += 1
        endWhile
    endif

    int presetSlot = 0

    if (hasUserPresets)
        AddHeaderOption("$SLID_HeaderUserPresets")
        int upi = 0
        while (upi < presetCount && upi < 10 && presetSlot < 10)
            if (SLID_Native.IsPresetUserGenerated(presetNameList[upi]))
                _presetNames[presetSlot] = presetNameList[upi]
                string status = SLID_Native.GetPresetStatus(presetNameList[upi])
                if (status == "Available")
                    _oidPresets[presetSlot] = AddTextOption("<font color='#999999'>" + presetNameList[upi] + "</font>", "$SLID_Activate")
                else
                    _oidPresets[presetSlot] = AddTextOption("<font color='#666666'>" + presetNameList[upi] + "</font>", "$SLID_Unavailable", OPTION_FLAG_DISABLED)
                endif
                presetSlot += 1
            endif
            upi += 1
        endWhile
        AddEmptyOption()
    endif

    ; Mod-authored presets section
    bool hasModPresets = false
    if (presetCount > 0)
        int mCheck = 0
        while (mCheck < presetCount && mCheck < 10)
            if (!SLID_Native.IsPresetUserGenerated(presetNameList[mCheck]))
                hasModPresets = true
            endif
            mCheck += 1
        endWhile
    endif

    if (hasModPresets)
        AddHeaderOption("$SLID_HeaderPresets")
        int mi = 0
        while (mi < presetCount && mi < 10 && presetSlot < 10)
            if (!SLID_Native.IsPresetUserGenerated(presetNameList[mi]))
                _presetNames[presetSlot] = presetNameList[mi]
                string status2 = SLID_Native.GetPresetStatus(presetNameList[mi])
                if (status2 == "Available")
                    _oidPresets[presetSlot] = AddTextOption("<font color='#999999'>" + presetNameList[mi] + "</font>", "$SLID_Activate")
                else
                    _oidPresets[presetSlot] = AddTextOption("<font color='#666666'>" + presetNameList[mi] + "</font>", "$SLID_Unavailable", OPTION_FLAG_DISABLED)
                endif
                presetSlot += 1
            endif
            mi += 1
        endWhile
        AddEmptyOption()
    endif

    ; Container lists section
    int clCount = SLID_Native.GetContainerListCount()
    if (clCount > 0)
        string[] clNames = SLID_Native.GetContainerListNames()
        AddHeaderOption("$SLID_HeaderContainerLists")
        int ci = 0
        while (ci < clCount && ci < 10)
            _containerListNames[ci] = clNames[ci]
            _oidContainerLists[ci] = AddToggleOption(clNames[ci], SLID_Native.IsContainerListEnabled(clNames[ci]))
            ci += 1
        endWhile
        AddEmptyOption()
    endif

endFunction

; =============================================================================
; ABOUT PAGE
; =============================================================================

function RenderAboutPage()
    SetCursorFillMode(TOP_TO_BOTTOM)

    AddHeaderOption("$SLID_HeaderAbout")
    AddTextOption("$SLID_FullName", "v" + SLID_Native.GetPluginVersion())
    AddTextOption("$SLID_Author", "ohfor")

    AddEmptyOption()

    AddHeaderOption("$SLID_HeaderLinks")
    AddTextOption("$SLID_Patreon", "patreon.com/ohfor")
    AddTextOption("$SLID_GitHub", "github.com/ohfor/slid")

    AddEmptyOption()

    AddHeaderOption("$SLID_HeaderCredits")
    AddTextOption("$SLID_CreditSKSE", "")
    AddTextOption("$SLID_CreditCommonLib", "")
    AddTextOption("$SLID_CreditSkyUI", "")
endFunction

; =============================================================================
; OPTION EVENTS
; =============================================================================

event OnOptionSelect(int a_option)
    ; Toggle options
    if (a_option == _oidModEnabled)
        bool newVal = !SLID_Native.GetModEnabled()
        SLID_Native.SetModEnabled(newVal)
        SetToggleOptionValue(a_option, newVal)
        return
    endif

    if (a_option == _oidDebugLogging)
        bool newVal = !SLID_Native.GetDebugLogging()
        SLID_Native.SetDebugLogging(newVal)
        SetToggleOptionValue(a_option, newVal)
        return
    endif

    if (a_option == _oidSummonEnabled)
        bool newVal = !SLID_Native.GetSummonEnabled()
        SLID_Native.SetSummonEnabled(newVal)
        SetToggleOptionValue(a_option, newVal)
        return
    endif

    if (a_option == _oidShowWelcome)
        ; Toggle is inverted: checked = "show welcome on next interaction"
        bool currentlyShown = SLID_Native.GetShownWelcomeTutorial()
        SLID_Native.SetShownWelcomeTutorial(!currentlyShown)
        SetToggleOptionValue(a_option, currentlyShown)  ; inverted display
        return
    endif

    if (a_option == _oidSCIEIntegration)
        bool newVal = !SLID_Native.GetSCIEIntegration()
        SLID_Native.SetSCIEIntegration(newVal)
        SetToggleOptionValue(a_option, newVal)
        ; Refresh page to update child toggle enabled state
        ForcePageReset()
        return
    endif

    if (a_option == _oidIncludeUnlinkedContainers)
        bool newVal = !SLID_Native.GetIncludeUnlinkedContainers()
        SLID_Native.SetIncludeUnlinkedContainers(newVal)
        SetToggleOptionValue(a_option, newVal)
        return
    endif

    if (a_option == _oidIncludeSCIEContainers)
        bool newVal = !SLID_Native.GetIncludeSCIEContainers()
        SLID_Native.SetIncludeSCIEContainers(newVal)
        SetToggleOptionValue(a_option, newVal)
        return
    endif

    ; Text options (actions)
    if (a_option == _oidGrantPowers)
        SLID_Native.RefreshPowers()
        ShowMessage("$SLID_PowersGranted", false)
        return
    endif

    if (a_option == _oidResetAllData)
        if (ShowMessage("$SLID_ConfirmResetAll", true))
            SLID_Native.RemoveAllNetworks()
            ShowMessage("$SLID_ResetComplete", false)
            ForcePageReset()
        endif
        return
    endif

    if (a_option == _oidSellDefaults)
        ; Reset General Sales to defaults: 10%, 10 items, 24 hours
        SLID_Native.SetSellPricePercent(0.10)
        SLID_Native.SetSellBatchSize(10)
        SLID_Native.SetSellIntervalHours(24.0)
        ForcePageReset()
        return
    endif

    if (a_option == _oidWholesaleDefaults)
        ; Reset Wholesale to defaults: 25%, 25 items, 48 hours, 5000 gold
        SLID_Native.SetVendorPricePercent(0.25)
        SLID_Native.SetVendorBatchSize(25)
        SLID_Native.SetVendorIntervalHours(48.0)
        SLID_Native.SetVendorCost(5000)
        ForcePageReset()
        return
    endif

    if (a_option == _oidRunSort)
        if (_selectedNetwork != "")
            int moved = SLID_Native.RunSort(_selectedNetwork)
            ShowMessage("$SLID_SortComplete" + " (" + moved + " items)", false)
        endif
        return
    endif

    if (a_option == _oidRunSweep)
        if (_selectedNetwork != "")
            int gathered = SLID_Native.RunSweep(_selectedNetwork)
            ShowMessage("$SLID_SweepComplete" + " (" + gathered + " items)", false)
        endif
        return
    endif

    if (a_option == _oidDestroyLink)
        if (_selectedNetwork != "")
            if (ShowMessage("$SLID_ConfirmDestroyLink", true))
                SLID_Native.RemoveNetwork(_selectedNetwork)
                _selectedNetwork = ""
                ForcePageReset()
            endif
        endif
        return
    endif

    ; Preset activation
    int pi2 = 0
    while (pi2 < 10)
        if (a_option == _oidPresets[pi2] && _oidPresets[pi2] != -1)
            string presetName = _presetNames[pi2]
            ; Check for warnings (plugin conflicts)
            string warnings = SLID_Native.GetPresetWarnings(presetName)
            if (warnings != "")
                if (!ShowMessage(warnings, true))
                    return  ; User cancelled after seeing warning
                endif
            endif
            ; Check if preset's master is already used by an existing network
            string conflictNet = SLID_Native.GetPresetMasterConflict(presetName)
            if (conflictNet != "")
                if (!ShowMessage("Preset '" + presetName + "' uses the same Master as your '" + conflictNet + "' link. Do you want to replace your current link?", true))
                    return
                endif
                SLID_Native.RemoveNetwork(conflictNet)
            endif
            bool activated = SLID_Native.ActivatePreset(presetName)
            if (activated)
                ShowMessage("$SLID_PresetActivated", false)
            else
                ShowMessage("$SLID_PresetFailed", false)
            endif
            ForcePageReset()
            return
        endif
        pi2 += 1
    endWhile

    ; Container list toggles
    int cli = 0
    while (cli < 10)
        if (a_option == _oidContainerLists[cli] && _oidContainerLists[cli] != -1)
            bool currentVal = SLID_Native.IsContainerListEnabled(_containerListNames[cli])
            SLID_Native.SetContainerListEnabled(_containerListNames[cli], !currentVal)
            SetToggleOptionValue(a_option, !currentVal)
            return
        endif
        cli += 1
    endWhile

    ; Export link — per-row export buttons
    int eli = 0
    while (eli < 10)
        if (a_option == _oidExportLinks[eli] && _oidExportLinks[eli] != -1)
            SLID_Native.BeginGeneratePreset(_exportLinkNames[eli])
            return
        endif
        eli += 1
    endWhile
endEvent

event OnOptionMenuOpen(int a_option)
    if (a_option == _oidNetworkSelector)
        SetMenuDialogOptions(_networkNames)
        int startIdx = 0
        int i = 0
        while (i < _networkNames.Length)
            if (_networkNames[i] == _selectedNetwork)
                startIdx = i
            endif
            i += 1
        endWhile
        SetMenuDialogStartIndex(startIdx)
        SetMenuDialogDefaultIndex(0)
    endif

endEvent

event OnOptionMenuAccept(int a_option, int a_index)
    if (a_option == _oidNetworkSelector)
        if (a_index >= 0 && a_index < _networkNames.Length)
            _selectedNetwork = _networkNames[a_index]
            SetMenuOptionValue(a_option, _selectedNetwork)
            ForcePageReset()
        endif
    endif

endEvent

event OnOptionSliderOpen(int a_option)
    if (a_option == _oidSellPricePercent)
        SetSliderDialogStartValue(SLID_Native.GetSellPricePercent() * 100.0)
        SetSliderDialogDefaultValue(10.0)
        SetSliderDialogRange(1.0, 100.0)
        SetSliderDialogInterval(1.0)
        return
    endif

    if (a_option == _oidSellBatchSize)
        SetSliderDialogStartValue(SLID_Native.GetSellBatchSize() as float)
        SetSliderDialogDefaultValue(10.0)
        SetSliderDialogRange(1.0, 100.0)
        SetSliderDialogInterval(1.0)
        return
    endif

    if (a_option == _oidSellIntervalHours)
        SetSliderDialogStartValue(SLID_Native.GetSellIntervalHours())
        SetSliderDialogDefaultValue(24.0)
        SetSliderDialogRange(1.0, 168.0)
        SetSliderDialogInterval(1.0)
        return
    endif

    if (a_option == _oidVendorPricePercent)
        SetSliderDialogStartValue(SLID_Native.GetVendorPricePercent() * 100.0)
        SetSliderDialogDefaultValue(25.0)
        SetSliderDialogRange(1.0, 100.0)
        SetSliderDialogInterval(1.0)
        return
    endif

    if (a_option == _oidVendorBatchSize)
        SetSliderDialogStartValue(SLID_Native.GetVendorBatchSize() as float)
        SetSliderDialogDefaultValue(25.0)
        SetSliderDialogRange(1.0, 100.0)
        SetSliderDialogInterval(1.0)
        return
    endif

    if (a_option == _oidVendorIntervalHours)
        SetSliderDialogStartValue(SLID_Native.GetVendorIntervalHours())
        SetSliderDialogDefaultValue(48.0)
        SetSliderDialogRange(1.0, 336.0)
        SetSliderDialogInterval(1.0)
        return
    endif

    if (a_option == _oidVendorCost)
        SetSliderDialogStartValue(SLID_Native.GetVendorCost() as float)
        SetSliderDialogDefaultValue(5000.0)
        SetSliderDialogRange(0.0, 50000.0)
        SetSliderDialogInterval(100.0)
        return
    endif
endEvent

event OnOptionSliderAccept(int a_option, float a_value)
    if (a_option == _oidSellPricePercent)
        SLID_Native.SetSellPricePercent(a_value / 100.0)
        SetSliderOptionValue(a_option, a_value, "{0}%")
        return
    endif

    if (a_option == _oidSellBatchSize)
        SLID_Native.SetSellBatchSize(a_value as int)
        SetSliderOptionValue(a_option, a_value, "{0}")
        return
    endif

    if (a_option == _oidSellIntervalHours)
        SLID_Native.SetSellIntervalHours(a_value)
        SetSliderOptionValue(a_option, a_value, "{1} hrs")
        return
    endif

    if (a_option == _oidVendorPricePercent)
        SLID_Native.SetVendorPricePercent(a_value / 100.0)
        SetSliderOptionValue(a_option, a_value, "{0}%")
        return
    endif

    if (a_option == _oidVendorBatchSize)
        SLID_Native.SetVendorBatchSize(a_value as int)
        SetSliderOptionValue(a_option, a_value, "{0}")
        return
    endif

    if (a_option == _oidVendorIntervalHours)
        SLID_Native.SetVendorIntervalHours(a_value)
        SetSliderOptionValue(a_option, a_value, "{1} hrs")
        return
    endif

    if (a_option == _oidVendorCost)
        SLID_Native.SetVendorCost(a_value as int)
        SetSliderOptionValue(a_option, a_value, "{0} gold")
        return
    endif
endEvent

event OnOptionHighlight(int a_option)
    if (a_option == _oidModEnabled)
        SetInfoText("$SLID_ModEnabledDesc")
        return
    endif

    if (a_option == _oidDebugLogging)
        SetInfoText("$SLID_DebugLoggingDesc")
        return
    endif

    if (a_option == _oidSummonEnabled)
        SetInfoText("$SLID_SummonEnabledDesc")
        return
    endif

    if (a_option == _oidShowWelcome)
        SetInfoText("$SLID_ShowWelcomeDesc")
        return
    endif

    if (a_option == _oidGrantPowers)
        SetInfoText("$SLID_GrantPowersDesc")
        return
    endif

    if (a_option == _oidResetAllData)
        SetInfoText("$SLID_ResetAllDataDesc")
        return
    endif

    if (a_option == _oidSellDefaults)
        SetInfoText("$SLID_SellDefaultsDesc")
        return
    endif

    if (a_option == _oidWholesaleDefaults)
        SetInfoText("$SLID_WholesaleDefaultsDesc")
        return
    endif

    if (a_option == _oidSellPricePercent)
        SetInfoText("$SLID_SellPricePercentDesc")
        return
    endif

    if (a_option == _oidSellBatchSize)
        SetInfoText("$SLID_SellBatchSizeDesc")
        return
    endif

    if (a_option == _oidSellIntervalHours)
        SetInfoText("$SLID_SellIntervalHoursDesc")
        return
    endif

    if (a_option == _oidVendorPricePercent)
        SetInfoText("$SLID_VendorPricePercentDesc")
        return
    endif

    if (a_option == _oidVendorBatchSize)
        SetInfoText("$SLID_VendorBatchSizeDesc")
        return
    endif

    if (a_option == _oidVendorIntervalHours)
        SetInfoText("$SLID_VendorIntervalHoursDesc")
        return
    endif

    if (a_option == _oidVendorCost)
        SetInfoText("$SLID_VendorCostDesc")
        return
    endif

    if (a_option == _oidRunSort)
        SetInfoText("$SLID_RunSortDesc")
        return
    endif

    if (a_option == _oidRunSweep)
        SetInfoText("$SLID_RunSweepDesc")
        return
    endif

    if (a_option == _oidDestroyLink)
        SetInfoText("$SLID_DestroyLinkDesc")
        return
    endif

    if (a_option == _oidNetworkSelector)
        SetInfoText("$SLID_SelectLinkDesc")
        return
    endif

    if (a_option == _oidSCIEIntegration)
        SetInfoText("$SLID_SCIEIntegrationDesc")
        return
    endif

    if (a_option == _oidIncludeUnlinkedContainers)
        SetInfoText("$SLID_IncludeUnlinkedContainersDesc")
        return
    endif

    if (a_option == _oidIncludeSCIEContainers)
        SetInfoText("$SLID_IncludeSCIEContainersDesc")
        return
    endif

    ; Export link info
    int eli2 = 0
    while (eli2 < 10)
        if (a_option == _oidExportLinks[eli2] && _oidExportLinks[eli2] != -1)
            SetInfoText("$SLID_GenerateExportDesc")
            return
        endif
        eli2 += 1
    endWhile

    ; Preset info — per-preset description
    int pi3 = 0
    while (pi3 < 10)
        if (a_option == _oidPresets[pi3] && _oidPresets[pi3] != -1)
            string desc = SLID_Native.GetPresetDescription(_presetNames[pi3])
            if (desc != "")
                SetInfoText(desc)
            else
                SetInfoText("$SLID_PresetDesc")
            endif
            return
        endif
        pi3 += 1
    endWhile

    ; Containerlist info
    int ci = 0
    while (ci < 10)
        if (a_option == _oidContainerLists[ci] && _oidContainerLists[ci] != -1)
            string clDesc = SLID_Native.GetContainerListDescription(_containerListNames[ci])
            if (clDesc != "")
                SetInfoText(clDesc)
            else
                SetInfoText("$SLID_ContainerListDesc")
            endif
            return
        endif
        ci += 1
    endWhile

    ; Vendor info - plain string (no $ prefix = displayed as-is)
    int i = 0
    while (i < 10)
        if (a_option == _oidVendors[i] && _oidVendors[i] != -1)
            SetInfoText(_vendorInfoTexts[i])
            return
        endif
        i += 1
    endWhile
endEvent
