#import "PrivilegedInstaller.h"

#import <Security/Security.h>
#import <ServiceManagement/ServiceManagement.h>

NSInteger SquidAuthorizeAndInstall(NSString *installerPath) {
    AuthorizationRef authorization = NULL;
    OSStatus status = AuthorizationCreate(
        NULL,
        kAuthorizationEmptyEnvironment,
        kAuthorizationFlagDefaults,
        &authorization
    );
    if (status != errAuthorizationSuccess) {
        return status;
    }

    AuthorizationItem right = {
        kSMRightModifySystemDaemons,
        0,
        NULL,
        0
    };
    AuthorizationRights rights = {
        1,
        &right
    };

    status = AuthorizationCopyRights(
        authorization,
        &rights,
        kAuthorizationEmptyEnvironment,
        kAuthorizationFlagInteractionAllowed | kAuthorizationFlagExtendRights,
        NULL
    );
    if (status == errAuthorizationSuccess) {
        char *arguments[] = { "install", NULL };
        status = AuthorizationExecuteWithPrivileges(
            authorization,
            installerPath.fileSystemRepresentation,
            kAuthorizationFlagDefaults,
            arguments,
            NULL
        );
    }

    AuthorizationFree(authorization, kAuthorizationFlagDefaults);
    return status;
}
