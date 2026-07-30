/** @file
  NucRedfishCredentialLib - RedfishPlatformCredentialLib instance declaring that
  the BMC's Redfish service needs no credentials.

  RedfishPkg ships PlatformCredentialLibNull, whose GetAuthInfo returns
  EFI_UNSUPPORTED. That is not the same statement as "this service is
  unauthenticated", and RedfishHttpDxe treats the difference as fatal:

      RedfishCredential2GetAuthInfo: Failed to retrieve Redfish credential - Unsupported
      RedfishCreateRedfishService: cannot get authentication information: Unsupported

  ...and RedfishCreateService returns NULL, so no request is ever made. Observed
  on hardware 2026-07-30 with the Null instance wired in.

  The correct answer for this platform is AuthMethodNone. The link is a
  point-to-point CDC-ECM cable between exactly one host and one BMC, carrying no
  other traffic and not routed (see configureEthernetGadgetInterface() in the
  JetKVM's usb.go, which gives usb0 a link-local address and no gateway). The
  BMC accepts unauthenticated requests arriving on that subnet and only that
  subnet -- isRedfishHostInterfaceRequest() in its redfish.go -- which is the
  boundary this rests on.

  The specified alternative, DSP0270 bootstrap credentials, is delivered over
  IPMI; this board has no IPMI transport, which is why
  PcdRedfishDisableBootstrapCredentialService is set.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Protocol/EdkIIRedfishCredential.h>

/**
  Return the Redfish authentication method and credentials for this platform.

  @param[in]   This        Pointer to EDKII_REDFISH_CREDENTIAL_PROTOCOL instance.
  @param[out]  AuthMethod  Receives AuthMethodNone.
  @param[out]  UserId      Receives NULL: no user id is used.
  @param[out]  Password    Receives NULL: no password is used.

  @retval EFI_SUCCESS            Authentication information returned.
  @retval EFI_INVALID_PARAMETER  An output pointer is NULL.
**/
EFI_STATUS
EFIAPI
LibCredentialGetAuthInfo (
  IN  EDKII_REDFISH_CREDENTIAL_PROTOCOL  *This,
  OUT EDKII_REDFISH_AUTH_METHOD          *AuthMethod,
  OUT CHAR8                              **UserId,
  OUT CHAR8                              **Password
  )
{
  if ((This == NULL) || (AuthMethod == NULL) || (UserId == NULL) || (Password == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // RedfishHttpDxe only inspects UserId/Password when AuthMethod is not
  // AuthMethodNone, and skips building the Authorization header entirely in
  // that case -- which is what the BMC expects over the host interface.
  //
  *AuthMethod = AuthMethodNone;
  *UserId     = NULL;
  *Password   = NULL;

  DEBUG ((DEBUG_ERROR, "NucRedfishCredential: host interface is unauthenticated (AuthMethodNone)\n"));

  return EFI_SUCCESS;
}

/**
  Stop the Redfish service. There is no credential state to revoke, so nothing
  has to happen here; reporting success keeps callers from treating a clean
  shutdown as a failure.

  @param[in]  This           Pointer to EDKII_REDFISH_CREDENTIAL_PROTOCOL instance.
  @param[in]  ServiceStopType  Type of stop request.

  @retval EFI_SUCCESS            Nothing to do.
  @retval EFI_INVALID_PARAMETER  This is NULL or the stop type is out of range.
**/
EFI_STATUS
EFIAPI
LibStopRedfishService (
  IN     EDKII_REDFISH_CREDENTIAL_PROTOCOL          *This,
  IN     EDKII_REDFISH_CREDENTIAL_STOP_SERVICE_TYPE  ServiceStopType
  )
{
  if ((This == NULL) || (ServiceStopType >= ServiceStopTypeMax)) {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

/**
  Notification of Exit Boot Service. Nothing to tear down.

  @param[in]  This  Pointer to EDKII_REDFISH_CREDENTIAL_PROTOCOL instance.
**/
VOID
EFIAPI
LibCredentialExitBootServicesNotify (
  IN  EDKII_REDFISH_CREDENTIAL_PROTOCOL  *This
  )
{
  return;
}

/**
  Notification of End of DXE. Nothing to tear down.

  @param[in]  This  Pointer to EDKII_REDFISH_CREDENTIAL_PROTOCOL instance.
**/
VOID
EFIAPI
LibCredentialEndOfDxeNotify (
  IN  EDKII_REDFISH_CREDENTIAL_PROTOCOL  *This
  )
{
  return;
}
