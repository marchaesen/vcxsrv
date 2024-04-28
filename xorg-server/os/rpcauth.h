#ifndef _XSERVER_OS_RPCAUTH_H
#define _XSERVER_OS_RPCAUTH_H

#include "auth.h"

void SecureRPCInit(AuthInitArgs);
XID SecureRPCCheck(AuthCheckArgs);
int SecureRPCAdd(AuthAddCArgs);
int SecureRPCFromID(AuthFromIDArgs);
int SecureRPCRemove(AuthRemCArgs);
int SecureRPCReset(AuthRstCArgs);

#endif /* _XSERVER_OS_RPCAUTH_H */
