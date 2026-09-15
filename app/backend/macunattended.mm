#include "macunattended.h"
#import <ServiceManagement/ServiceManagement.h>
static SMAppService* recoveryService() {
    return [SMAppService daemonServiceWithPlistName:@"io.github.keithxc.DeskPort.Recovery.plist"];
}
int deskPortUnattendedServiceStatus() {
    @autoreleasepool { return static_cast<int>(recoveryService().status); }
}
bool deskPortSetUnattendedService(bool enabled, QString& error) {
    @autoreleasepool {
        SMAppService* service = recoveryService();
        if ((enabled && service.status == SMAppServiceStatusEnabled) ||
            (!enabled && service.status == SMAppServiceStatusNotRegistered)) return true;
        NSError* nativeError = nil;
        const BOOL result = enabled ? [service registerAndReturnError:&nativeError]
                                    : [service unregisterAndReturnError:&nativeError];
        // A pending approval is an actionable state, never an "enabled" success.
        if (enabled && service.status == SMAppServiceStatusRequiresApproval) return true;
        if (!result) error = QString::fromUtf8(nativeError.localizedDescription.UTF8String);
        return result;
    }
}
void deskPortOpenBackgroundItems() {
    [SMAppService openSystemSettingsLoginItems];
}
