#ifndef NUC_CAPSULE_TEST_UEFI_H_
#define NUC_CAPSULE_TEST_UEFI_H_
#include <stdint.h>
#include <stddef.h>
#include <uchar.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;
typedef uint64_t UINT64; typedef size_t UINTN; typedef int BOOLEAN;
typedef char16_t CHAR16;   // NOT uint16_t: u"..." literals are char16_t*,
                           // and -Werror rejects the pointer mismatch.
#define CONST const
#define IN
#define OUT
#define VOID void
#define STATIC static
#define TRUE 1
#define FALSE 0
typedef UINTN EFI_STATUS;
#define EFI_SUCCESS            0
#define EFI_INVALID_PARAMETER  2
#define EFI_UNSUPPORTED        3
#define EFI_BAD_BUFFER_SIZE    4
typedef struct { UINT32 Data1; UINT16 Data2; UINT16 Data3; UINT8 Data4[8]; } EFI_GUID;
#define CAPSULE_FLAGS_PERSIST_ACROSS_RESET  0x00010000
#define CAPSULE_FLAGS_POPULATE_SYSTEM_TABLE 0x00020000
#define CAPSULE_FLAGS_INITIATE_RESET        0x00040000
#endif
