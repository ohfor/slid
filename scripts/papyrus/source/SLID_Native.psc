ScriptName SLID_Native

; =============================================================================
; Core Functions
; =============================================================================

string Function SetMasterAuto() global native
Function BeginTagContainer() global native
Function BeginDeregister() global native
Function BeginDetect() global native
string Function GetMasterNetwork() global native
Function RemoveNetwork(string asName) global native
Function RemoveAllNetworks() global native
Function RefreshPowers() global native
Function BeginSellContainer() global native
Function BeginSummonChest() global native
Function DespawnSummonChest() global native
Function ShowConfigMenu() global native
Function HideConfigMenu() global native

; =============================================================================
; MCM Settings - General
; =============================================================================

bool Function GetModEnabled() global native
Function SetModEnabled(bool abEnabled) global native

bool Function GetDebugLogging() global native
Function SetDebugLogging(bool abEnabled) global native

; =============================================================================
; MCM Settings - Container Picker
; =============================================================================

bool Function GetIncludeUnlinkedContainers() global native
Function SetIncludeUnlinkedContainers(bool abEnabled) global native

bool Function GetIncludeSCIEContainers() global native
Function SetIncludeSCIEContainers(bool abEnabled) global native

; =============================================================================
; MCM Settings - Sales
; =============================================================================

float Function GetSellPricePercent() global native
Function SetSellPricePercent(float afValue) global native

int Function GetSellBatchSize() global native
Function SetSellBatchSize(int aiValue) global native

float Function GetSellIntervalHours() global native
Function SetSellIntervalHours(float afValue) global native

float Function GetVendorPricePercent() global native
Function SetVendorPricePercent(float afValue) global native

int Function GetVendorBatchSize() global native
Function SetVendorBatchSize(int aiValue) global native

float Function GetVendorIntervalHours() global native
Function SetVendorIntervalHours(float afValue) global native

int Function GetVendorCost() global native
Function SetVendorCost(int aiValue) global native

; =============================================================================
; MCM Settings - Maintenance
; =============================================================================

bool Function GetSummonEnabled() global native
Function SetSummonEnabled(bool abEnabled) global native

bool Function GetShownWelcomeTutorial() global native
Function SetShownWelcomeTutorial(bool abShown) global native

; =============================================================================
; MCM Link Page
; =============================================================================

int Function GetNetworkCount() global native
string[] Function GetNetworkNames() global native
string Function GetNetworkMasterName(string asNetworkName) global native
int Function RunSort(string asNetworkName) global native
int Function RunSweep(string asNetworkName) global native
int Function GetNetworkContainerCount(string asNetworkName) global native
string[] Function GetNetworkContainerNames(string asNetworkName) global native
Function RemoveContainerFromNetwork(string asNetworkName, int aiIndex) global native

; =============================================================================
; MCM Presets
; =============================================================================

int Function GetPresetCount() global native
string[] Function GetPresetNames() global native
string Function GetPresetStatus(string asName) global native
string Function GetPresetWarnings(string asName) global native
bool Function ActivatePreset(string asName) global native
string Function GetPresetMasterConflict(string asName) global native
Function ReloadPresets() global native

string Function GetPresetDescription(string asName) global native
bool Function IsPresetUserGenerated(string asName) global native

; =============================================================================
; MCM Containerlists
; =============================================================================

int Function GetContainerListCount() global native
string[] Function GetContainerListNames() global native
string Function GetContainerListDescription(string asName) global native
bool Function IsContainerListEnabled(string asName) global native
Function SetContainerListEnabled(string asName, bool abEnabled) global native

; =============================================================================
; MCM Compatibility
; =============================================================================

bool Function IsTCCInstalled() global native
bool Function IsSCIEInstalled() global native

bool Function GetSCIEIntegration() global native
Function SetSCIEIntegration(bool abEnabled) global native

bool Function GetSCIEIncludeContainers() global native
Function SetSCIEIncludeContainers(bool abEnabled) global native

; =============================================================================
; MCM Wholesale Arrangements (Vendor Info)
; =============================================================================

int Function GetRegisteredVendorCount() global native
string[] Function GetRegisteredVendorNames() global native
string Function GetVendorStoreName(int aiIndex) global native
string Function GetVendorCategories(int aiIndex) global native
float Function GetVendorBonusPercent(int aiIndex) global native
string Function GetVendorLastVisit(int aiIndex) global native

; =============================================================================
; MCM About
; =============================================================================

string Function GetPluginVersion() global native

; =============================================================================
; MCM Presets / Debug
; =============================================================================

bool Function GeneratePresetINI(string asNetworkName, string asPresetName) global native
Function BeginGeneratePreset(string asNetworkName) global native
Function DumpContainers() global native
Function DumpFilters() global native
Function DumpVendors() global native

; =============================================================================
; Legacy Functions (deprecated, kept for backwards compatibility)
; =============================================================================

Function BeginLinkContainer() global native
Function BeginDismantleNetwork() global native
