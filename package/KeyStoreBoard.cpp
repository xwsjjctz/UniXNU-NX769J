// SPDX-License-Identifier: BSD-2-Clause-Patent
// Board side of drivers/AppleKeyStoreState for NX769J.
//
// The board has no SEP, and the iOS 16.5 kernelcache carries no AppleSEPKeyStore, so keybagd finds no AppleKeyStore,
// cannot load the system bag and reboots into recovery. With sheng-aks=1 this publishes the AppleKeyStoreState stand-in
// on IOResources before launchd starts. It answers the state queries of a device with no passcode and nothing locked;
// it holds no key and protects nothing.
//
// The hooks follow UniXNU-sheng package/KeyStoreProbe.cpp (08cde20e), without its diagnostic page rows.
#include <IOKit/IOService.h>
#include <libkern/c++/OSSymbol.h>
#include <pexpert/pexpert.h>

extern "C" IOService *native_keyevents_allocate(void);

extern "C" {
volatile uint32_t native_keyevents_refused_opens, native_aks_opens, native_aks_calls;
volatile int nx769j_keystore_status = -1;

void native_keyevents_open_refused(void) { native_keyevents_refused_opens = native_keyevents_refused_opens + 1; }

bool native_aks_state_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        unsigned value = 0;
        cached = PE_parse_boot_argn("sheng-aks", &value, sizeof(value)) && value == 1;
    }
    return cached == 1;
}

// sheng-aks-sls=<n> overrides the reported lock state (default 0 = unlocked; 3 = MobileKeyBag's "no passcode").
uint32_t native_aks_lock_state(void) {
    static int cached = -1;
    if (cached < 0) {
        unsigned value = 0;
        if (!PE_parse_boot_argn("sheng-aks-sls", &value, sizeof(value)) || value > 7) value = 0;
        cached = (int)value;
    }
    return (uint32_t)cached;
}

void native_aks_opened(void) {
    native_aks_opens = native_aks_opens + 1;
    IOLog("NX769J AKS: open %u\n", native_aks_opens);
}

// Every selector with its answer, so the panel shows what keybagd and the other daemons ask for.
void native_aks_call(uint32_t selector, int32_t result) {
    native_aks_calls = native_aks_calls + 1;
    IOLog("NX769J AKS: selector %u -> 0x%x\n", selector, (uint32_t)result);
}

// Called from bsd_init before load_init_program. Returns the IOReturn of the attempt.
int nx769j_keystore_publish(void) {
    if (!native_aks_state_enabled()) return nx769j_keystore_status = kIOReturnNotPermitted;
    const OSSymbol *name = OSSymbol::withCString("AppleKeyStore");
    const OSMetaClass *existing = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
    OSSafeReleaseNULL(name);
    // The stand-in registers its own AppleKeyStore class. Any other one would be a real keystore: leave it alone.
    if (!existing || strcmp(existing->getClassName(), "AppleKeyStore") || existing->getKmodName() == nullptr ||
        strcmp(existing->getKmodName()->getCStringNoCopy(), "__kernel__")) {
        return nx769j_keystore_status = kIOReturnUnsupported;
    }
    IOService *provider = IOService::getResourceService();
    IOService *service = provider ? native_keyevents_allocate() : nullptr;
    bool initialized = service && service->init();
    bool attached = initialized && service->attach(provider);
    bool started = attached && service->start(provider);
    if (!started) {
        if (attached) service->detach(provider);
        OSSafeReleaseNULL(service);
        return nx769j_keystore_status = kIOReturnError;
    }
    service->release();
    IOLog("NX769J AKS: AppleKeyStore stand-in published (no passcode, unlocked, no keys)\n");
    return nx769j_keystore_status = kIOReturnSuccess;
}
}
