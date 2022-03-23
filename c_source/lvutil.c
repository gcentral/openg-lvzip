/* 
   lvutil.c -- support functions for LabVIEW ZIP library

   Version 1.29, Oct 27, 2019

   Copyright (C) 2002-2019 Rolf Kalbermatter

   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
   following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the
	   following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
       following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of SciWare, James Kring, Inc., nor the names of its contributors may be used to endorse
	   or promote products derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define ZLIB_INTERNAL
#include "lvutil.h"
#include "zlib.h"
#include "ioapi.h"
#include "iomem.h"
#include "refnum.h"
#include "utf.h"
#if Unix
 #ifndef __USE_FILE_OFFSET64
  #define __USE_FILE_OFFSET64
 #endif
 #ifndef __USE_LARGEFILE64
  #define __USE_LARGEFILE64
 #endif
 #ifndef _LARGEFILE64_SOURCE
  #define _LARGEFILE64_SOURCE
 #endif
 #ifndef _FILE_OFFSET_BIT
  #define _FILE_OFFSET_BIT 64
 #endif
#endif
//#include <stdio.h>
#include <string.h>
#if Win32
 #define COBJMACROS
 #include <objbase.h>
 #include <shobjidl.h> 
 #include <shlobj.h>
 #include <shellapi.h>
 #include "iowin.h"
 #include "versionhelper.h"

 #ifndef INVALID_FILE_ATTRIBUTES
  #define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
 #endif
 /* For backward compatibility define newer flags for Windows Vista and 7 so that it compiles properly. */
 #ifndef FOFX_ADDUNDORECORD
  #define FOFX_ADDUNDORECORD 0x20000000
 #endif
 #ifndef FOFX_RECYCLEONDELETE
  #define FOFX_RECYCLEONDELETE 0x00080000
 #endif
 #ifndef FOF_NO_UI
  #define FOF_NO_UI (FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR) // don't display any UI at all
 #endif
 #ifndef IO_REPARSE_TAG_SYMLINK
  #define IO_REPARSE_TAG_SYMLINK		0xA000000C
 #endif
 #ifndef SE_CREATE_SYMBOLIC_LINK_NAME
  #define SE_CREATE_SYMBOLIC_LINK_NAME	TEXT("SeCreateSymbolicLinkPrivilege")
 #endif
#if Pharlap
 static const char strWildcardPattern[] = "*.*";
 #define CreateFileLW(path, fl, sh, ds, op, se, ov) CreateFileA(LWPathBuf(path), fl, sh, ds, op, se, ov)
 #define CreateDirectoryLW(path, sec)		CreateDirectoryA(LWPathBuf(path), sec)
 #define GetFileAttributesLW(path)			GetFileAttributesA(LWPathBuf(path))
 #define SetFileAttributesLW(path, attr)	SetFileAttributesA(LWPathBuf(path), attr)
 #define FindFirstFileLW(path, findFiles)	FindFirstFileA(LWPathBuf(path), findFiles)
 #define FindNextFileLW(handle, findFiles)	FindNextFileA(handle, findFiles)
 #define RemoveDirectoryLW(path)			RemoveDirectoryA(LWPathBuf(path))
 #define DeleteFileLW(path)					DeleteFileA(LWPathBuf(path))
 #define MoveFileLW(pathFrom, pathTo)		MoveFileA(LWPathBuf(pathFrom), LWPathBuf(pathTo))
#else
 static const wchar_t strWildcardPattern[] = L"*.*";
 #define CreateFileLW(path, fl, sh, ds, op, se, ov) CreateFileW(LWPathBuf(path), fl, sh, ds, op, se, ov)
 #define CreateDirectoryLW(path, sec)		CreateDirectoryW(LWPathBuf(path), sec)
 #define GetFileAttributesLW(path)			GetFileAttributesW(LWPathBuf(path))
 #define SetFileAttributesLW(path, attr)	SetFileAttributesW(LWPathBuf(path), attr)
 #define FindFirstFileLW(path, findFiles)	FindFirstFileW(LWPathBuf(path), findFiles)
 #define FindNextFileLW(handle, findFiles)	FindNextFileW(handle, findFiles)
 #define RemoveDirectoryLW(path)			RemoveDirectoryW(LWPathBuf(path))
 #define DeleteFileLW(path)					DeleteFileW(LWPathBuf(path))
 #define MoveFileLW(pathFrom, pathTo)		MoveFileW(LWPathBuf(pathFrom), LWPathBuf(pathTo))
#endif

 #define SYMLINK_FLAG_RELATIVE 1
 typedef struct _REPARSE_DATA_BUFFER
 {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union
    {
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG Flags;                    // contains SYMLINK_FLAG_RELATIVE(1) or 0
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
        struct
        {
            UCHAR  DataBuffer[1];
        } GenericReparseBuffer;
    } DUMMYUNIONNAME;
 } REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

#define REPARSE_DATA_BUFFER_HEADER_SIZE   FIELD_OFFSET(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer)

#define DE_SAMEFILE			 0x71	// The source and destination files are the same file.
#define DE_MANYSRC1DEST		 0x72	// Multiple file paths were specified in the source buffer, but only one destination file path.
#define DE_DIFFDIR			 0x73	// Rename operation was specified but the destination path is a different directory. Use the move operation instead.
#define DE_ROOTDIR			 0x74	// The source is a root directory, which cannot be moved or renamed.
#define DE_OPCANCELLED		 0x75	// The operation was canceled by the user, or silently canceled if the appropriate flags were supplied to SHFileOperation.
#define DE_DESTSUBTREE		 0x76	// The destination is a subtree of the source.
#define DE_ACCESSDENIEDSRC	 0x78	// Security settings denied access to the source.
#define DE_PATHTOODEEP		 0x79	// The source or destination path exceeded or would exceed MAX_PATH.
#define DE_MANYDEST			 0x7A	// The operation involved multiple destination paths, which can fail in the case of a move operation.
#define DE_INVALIDFILES		 0x7C	// The path in the source or destination or both was invalid.
#define DE_DESTSAMETREE		 0x7D	// The source and destination have the same parent folder.
#define DE_FLDDESTISFILE	 0x7E	// The destination path is an existing file.
#define DE_FILEDESTISFLD	 0x80	// The destination path is an existing folder.
#define DE_FILENAMETOOLONG	 0x81	// The name of the file exceeds MAX_PATH.
#define DE_DEST_IS_CDROM	 0x82	// The destination is a read-only CD-ROM, possibly unformatted.
#define DE_DEST_IS_DVD		 0x83	// The destination is a read-only DVD, possibly unformatted.
#define DE_DEST_IS_CDRECORD	 0x84	// The destination is a writable CD-ROM, possibly unformatted.
#define DE_FILE_TOO_LARGE	 0x85	// The file involved in the operation is too large for the destination media or file system.
#define DE_SRC_IS_CDROM		 0x86	// The source is a read-only CD-ROM, possibly unformatted.
#define DE_SRC_IS_DVD		 0x87	// The source is a read-only DVD, possibly unformatted.
#define DE_SRC_IS_CDRECORD	 0x88	// The source is a writable CD-ROM, possibly unformatted.
#define DE_ERROR_MAX		 0xB7	// MAX_PATH was exceeded during the operation.
#define DE_UNKNOWN_ERROR	0x402	// An unknown error occurred. This is typically due to an invalid path in the source or destination. This error does not occur on Windows Vista and later.
#define ERRORONDEST		  0x10000	// An unspecified error occurred on the destination.
#elif Unix
 #include <errno.h>
 #include <dirent.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <sys/stat.h>
 #define ftruncate64 ftruncate
 #include <wchar.h>
 #if VxWorks
  #include <sys/types.h>
  #include <string.h>
  #include <utime.h>
//  #include <ioLib.h>
  #ifdef __GNUC__
   #define ___unused __attribute__((unused))
  #else
   #define ___unused
  #endif

  #ifndef S_ISLNK
   #define	S_ISLNK(mode)	((mode & S_IFMT) == S_IFLNK)	/* symlink special */
  #endif

  #if _WRS_VXWORKS_MAJOR < 6 || _WRS_VXWORKS_MAJOR == 6 && _WRS_VXWORKS_MINOR < 2
  inline int link(const char* path1 ___unused, const char* path2 ___unused)
  {
	// VxWorks < 6.2 has no link() support
    errno = ENOTSUP;
    return -1;
  }

  inline int chmod(const char* _path ___unused, mode_t _mode ___unused)
  {
	// VxWorks < 6.2 has no chmod() support
    errno = ENOTSUP;
    return -1;
  }
  #endif

  // Fake symlink handling by dummy functions:
  inline int symlink(const char* path1 ___unused, const char* path2 ___unused)
  {
    // vxWorks has no symlinks -> always return an error!
    errno = ENOTSUP;
    return -1;
  }

  inline ssize_t readlink(const char* path1 ___unused, char* path2 ___unused, size_t size ___unused)
  {
    // vxWorks has no symlinks -> always return an error!
    errno = EACCES;
    return -1;
  }
  #define lstat(path, statbuf) stat(path, statbuf) 
  #define FCNTL_PARAM3_CAST(param)  (int32)(param)
 #else
  #define FCNTL_PARAM3_CAST(param)  (param)
 #define st_atimespec     st_atim
 #define st_mtimespec     st_mtim
 #endif
#elif MacOSX
 #include <CoreFoundation/CoreFoundation.h>
 #include <CoreServices/CoreServices.h>
 #include <objc/runtime.h>
 #include <objc/message.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <sys/attr.h>
 #include <sys/xattr.h>
 #include <dirent.h>
 #define ftruncate64 ftruncate
 #if ProcessorType!=kX64
  #define MacIsInvisible(cpb) ((cpb).hFileInfo.ioFlFndrInfo.fdFlags & kIsInvisible)
  #define MacIsInvFolder(cpb) ((cpb).dirInfo.ioDrUsrWds.frFlags & kIsInvisible)
  #define MacIsDir(cpb)   ((cpb).nodeFlags & ioDirMask)
  #define MacIsStationery(cpb) ((cpb).hFileInfo.ioFlFndrInfo.fdFlags & kIsStationery)
  #define MacIsAlias(cpb) ((cpb).hFileInfo.ioFlFndrInfo.fdFlags & kIsAlias)
  #define kFileChanged    (1L<<7)
  static MgErr OSErrToLVErr(OSStatus err);
 #endif

#define kFinfoIsInvisible (OSSwapHostToBigConstInt16(kIsInvisible))

typedef	struct finderinfo {
    u_int32_t type;
    u_int32_t creator;
    u_int16_t fdFlags;
    u_int32_t location;
    u_int16_t reserved;
    u_int32_t extendedFileInfo[4];
} __attribute__ ((__packed__)) finderinfo;

typedef struct fileinfobuf {
    u_int32_t info_length;
    u_int32_t data[8];
} fileinfobuf;
#endif

#include "lwstr.h"

static MgErr lvFile_Status(LWPathHandle lwstr, int32 end, uInt16 *fileType, uInt16 *fileRights);
static MgErr lvFile_ReadLink(LWPathHandle pathName, LWPathHandle *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileType);

#ifdef HAVE_BZIP2
void bz_internal_error(int errcode);
void bz_internal_error(int errcode)
{
	// if we have a debug build then print the error in the LabVIEW debug console
#if DEBUG
	DbgPrintf((CStr)"BZIP2 internal error %ld occurred!!", errcode);
#else
    Unused(errcode);
#endif
}
#endif

#if usesHFSPath
static MgErr FSMakePathRef(Path path, FSRef *ref)
{
	int32 pathLen = -1;
	MgErr err = FPathToText(path, (LStrPtr)&pathLen);
	if (!err)
	{
		LStrHandle str = NULL;
		err = NumericArrayResize(uB, 1, (UHandle*)&str, pathLen + 1);
		if (!err)
		{
			LStrLen(*str) = pathLen;
			err = FPathToText(path, *str);
			if (!err)
			{
				DEBUGPRINTF(((CStr)"FPathToText: path = %z", path));
				err = OSErrToLVErr(FSPathMakeRef(LStrBuf(*str), ref, NULL));
				if (err)
				{
					DEBUGPRINTF(((CStr)"FSPathMakeRef: err = %ld, len = %ld, path = %s", err, LStrLen(*str), LStrBuf(*str)));
				}
			}
			DSDisposeHandle((UHandle)str);
		}
    }
    return err;
}

static MgErr OSErrToLVErr(OSStatus err)
{
    switch(err)
    {
      case mgNoErr:
        return mgNoErr;
      case rfNumErr:
      case paramErr:
      case nsvErr:
      case fnOpnErr:
      case bdNamErr:
        return mgArgErr;
      case vLckdErr:
      case wrPermErr:
      case wPrErr:
      case fLckdErr:
      case afpAccessDenied:
      case permErr:
        return fNoPerm;
      case fBsyErr:
      case opWrErr:
        return fIsOpen;
      case posErr:
      case eofErr:
        return fEOF;
      case dirNFErr:
      case fnfErr:
        return fNotFound;
      case dskFulErr:
        return fDiskFull;
      case dupFNErr:
        return fDupPath;
      case tmfoErr:
        return fTMFOpen;
      case memFullErr:
        return mFullErr;
      case afpObjectTypeErr:
      case afpContainsSharedErr:
      case afpInsideSharedErr:
      return fNotEnabled;
    }
    return fIOErr; /* fIOErr generally signifies some unknown file error */
}
#endif

#if Win32
/* int64 100ns intervals from Jan 1 1601 GMT to Jan 1 1904 GMT */
#define LV1904_FILETIME_OFFSET  0x0153b281e0fb4000
#define SECS_TO_FT_MULT			10000000

static void ATimeToFileTime(ATime128 *pt, FILETIME *pft)
{
	LARGE_INTEGER li;
	li.QuadPart = pt->u.f.val * SECS_TO_FT_MULT;
	li.QuadPart += (pt->u.f.fract >> 32) * SECS_TO_FT_MULT / 0x100000000L;
	li.QuadPart += LV1904_FILETIME_OFFSET;
	pft->dwHighDateTime = li.HighPart;
	pft->dwLowDateTime = li.LowPart;
}

static void FileTimeToATime(FILETIME *pft, ATime128 *pt)
{
	LARGE_INTEGER li;    
	li.LowPart = pft->dwLowDateTime;
	li.HighPart = pft->dwHighDateTime;
	li.QuadPart -= LV1904_FILETIME_OFFSET;
	pt->u.f.val = li.QuadPart / SECS_TO_FT_MULT;
	pt->u.f.fract = ((li.QuadPart - pt->u.f.val) * 0x100000000 / SECS_TO_FT_MULT) << 32;
}

static MgErr Win32ToLVFileErr(DWORD winErr)
{
    switch (winErr)
    {
		case ERROR_NOT_ENOUGH_MEMORY: return mFullErr;
	    case ERROR_NOT_LOCKED:
		case ERROR_INVALID_NAME:
		case ERROR_INVALID_PARAMETER: return mgArgErr;
		case ERROR_PATH_NOT_FOUND:
		case ERROR_DIRECTORY:
		case ERROR_FILE_NOT_FOUND:    return fNotFound;
		case ERROR_LOCK_VIOLATION:
		case ERROR_ACCESS_DENIED:
		case ERROR_SHARING_VIOLATION: return fNoPerm;
		case ERROR_ALREADY_EXISTS:
		case ERROR_FILE_EXISTS:       return fDupPath;
		case ERROR_NOT_SUPPORTED:     return mgNotSupported;
		case ERROR_NO_MORE_FILES:     return mgNoErr;
    }
    return fIOErr;   /* fIOErr generally signifies some unknown file error */
}

#if DEBUG
static MgErr SHErrToLVFileErr(int winErr)
{
    switch (winErr)
    {
	    case DE_SAMEFILE:             return fDupPath;
	    case DE_MANYSRC1DEST:
		case DE_DIFFDIR:
		case DE_ROOTDIR:
		case DE_DESTSUBTREE:
		case DE_MANYDEST:
		case DE_DESTSAMETREE:
		case DE_FLDDESTISFILE:
		case DE_FILEDESTISFLD:        return mgArgErr;
		case DE_OPCANCELLED:          return cancelError;
		case DE_ACCESSDENIEDSRC:
		case DE_DEST_IS_CDROM:
		case DE_DEST_IS_DVD:
		case DE_DEST_IS_CDRECORD:
		case DE_ROOTDIR | ERRORONDEST:return fNoPerm;
		case DE_PATHTOODEEP:
		case DE_INVALIDFILES:
		case DE_FILENAMETOOLONG:
		case DE_SRC_IS_CDROM:
		case DE_SRC_IS_DVD:
		case DE_SRC_IS_CDRECORD:	  return fNotFound;
		case DE_ERROR_MAX:            return fDiskFull;
		case DE_FILE_TOO_LARGE:       return mgNotSupported;
		case ERRORONDEST:             return fIOErr;
    }
    return Win32ToLVFileErr(winErr);
}
#endif

static MgErr HRESULTToLVErr(HRESULT winErr)
{
	switch (winErr)
	{
		case S_OK:                    return mgNoErr;
		case E_FAIL:
		case E_NOINTERFACE:
	    case E_POINTER:               return mgArgErr;
		case E_OUTOFMEMORY:           return mFullErr;
		case REGDB_E_CLASSNOTREG:
		case CLASS_E_NOAGGREGATION:   return mgNotSupported;
	}
    return fIOErr;   /* fIOErr generally signifies some unknown file error */
}

static MgErr Win32GetLVFileErr(void)
{
	return Win32ToLVFileErr(GetLastError());
}

static uInt16 LVFileTypeFromWinFlags(DWORD dwAttrs)
{
	uInt16 flags = 0;
	if (dwAttrs != INVALID_FILE_ATTRIBUTES)
	{
		if (dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT)
			flags |= kIsLink;
		if (!(dwAttrs & FILE_ATTRIBUTE_DIRECTORY))
			flags |= kIsFile;
		if (dwAttrs & FILE_ATTRIBUTE_HIDDEN)
		    flags |= kFIsInvisible;
		if (dwAttrs & FILE_ATTRIBUTE_COMPRESSED)
			flags |= kIsCompressed;
	}
	return flags;
}

#if !Pharlap
static WCHAR *extStr = L".LNK";

static int Win32CanBeShortCutLink(WCHAR *str, int32 len)
{
	int32 extLen = lwslen(extStr);
	return (len >= extLen && !_wcsicmp(str + len - extLen, extStr));
}

LibAPI(MgErr) Win32ResolveShortCut(LWPathHandle wSrc, LWPathHandle *wTgt, int32 resolveDepth, int32 *resolveCount, DWORD *dwAttrs)
{
	HRESULT err = noErr;
	IShellLinkW* psl;
	IPersistFile* ppf;
	WIN32_FIND_DATAW fileData;
	WCHAR tempPath[MAX_PATH * 4], *srcPath = LWPathBuf(wSrc);

	// Don't bother trying to resolve shortcut if it doesn't have a ".lnk" extension.
	if (!Win32CanBeShortCutLink(srcPath, LWPathLenGet(wSrc)))
		return cancelError;

	// Get a pointer to the IShellLink interface.
	err = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&psl);
	if (SUCCEEDED(err))
	{
		// Get a pointer to the IPersistFile interface.
		err = IShellLinkW_QueryInterface(psl, &IID_IPersistFile, (void**)&ppf);
		while (SUCCEEDED(err))
		{
			// Load the shortcut.
			err = IPersistFile_Load(ppf, srcPath, STGM_READ);
			if (SUCCEEDED(err) && resolveDepth > 0)
			{
				// Resolve the shortcut.
				err = IShellLinkW_Resolve(psl, NULL, SLR_NO_UI);
			}
			if (SUCCEEDED(err))
			{
				fileData.dwFileAttributes = INVALID_FILE_ATTRIBUTES;
				err = IShellLinkW_GetPath(psl, tempPath, MAX_PATH * 4, &fileData, resolveDepth > 0 ? 0 : SLGP_RAWPATH); 
				if (SUCCEEDED(err))
				{
					*dwAttrs = fileData.dwFileAttributes;
					if (err == S_OK)
					{
						int32 len = lwslen(tempPath);
						if (resolveCount)
						{
							(*resolveCount)++;
							if ((resolveDepth < 0 || resolveDepth > *resolveCount) && Win32CanBeShortCutLink(tempPath, len))
							{
								srcPath = tempPath;
								continue;
							}
						}
						if (*dwAttrs == INVALID_FILE_ATTRIBUTES)
							*dwAttrs = GetFileAttributesW(tempPath);
						
						if (wTgt && LWPathNCat(wTgt, 0, tempPath, len))
							err = E_OUTOFMEMORY;
					}
					else
					{
						/* Path couldn't be retrieved, is there any other way we can get the info? */
						fileData.cFileName;
					}
				}
			}
			// Release pointer to IPersistFile interface.
			IPersistFile_Release(ppf);
			break;
		}
		// Release pointer to IShellLink interface.
		IShellLinkA_Release(psl);
	}
	return HRESULTToLVErr(err);
}

static BOOL Win32ModifyBackupPrivilege(BOOL fEnable)
{
	HANDLE handle;
	BOOL success = OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &handle);
	if (success)
	{
		TOKEN_PRIVILEGES tokenPrivileges;
		// NULL for local system
		success = LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &tokenPrivileges.Privileges[0].Luid);
		if (success)
		{
			tokenPrivileges.PrivilegeCount = 1;
			tokenPrivileges.Privileges[0].Attributes = fEnable ? SE_PRIVILEGE_ENABLED : 0;
			success = AdjustTokenPrivileges(handle, FALSE, &tokenPrivileges, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
		}
		CloseHandle(handle);
	}
	return success;
}

LibAPI(MgErr) Win32ResolveSymLink(LWPathHandle wSrc, LWPathHandle *wTgt, int32 resolveDepth, int32 *resolveCount, DWORD *dwAttrs)
{
	HANDLE handle;
	MgErr err = noErr;
	DWORD bytes = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
	PREPARSE_DATA_BUFFER buffer = NULL;
	LWPathHandle wIntermediate = wTgt ? *wTgt : NULL;

	*dwAttrs = GetFileAttributesLW(wSrc);
	if (*dwAttrs == INVALID_FILE_ATTRIBUTES)
		return Win32GetLVFileErr();

	if (!(*dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT))
		return mgNoErr;

	// Need to acquire backup privileges in order to be able to retrieve a handle to a directory below or call symlink kernel entry points
	Win32ModifyBackupPrivilege(TRUE);
	do
	{
		// Open the link file or directory
		handle = CreateFileLW(wSrc, GENERIC_READ, 0 /*FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE */, 
			                  NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
		if (handle == INVALID_HANDLE_VALUE)
			return Win32GetLVFileErr();

		if (!buffer)
			buffer = (PREPARSE_DATA_BUFFER)DSNewPtr(bytes);
		if (buffer)
		{
			if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer, bytes, &bytes, NULL))
				err = Win32GetLVFileErr();
			else if (bytes < 9)
				err = fEOF;
		}
		else
			err = mFullErr;

		// Close the handle to our file so we're not locking it anymore.
		CloseHandle(handle);

		if (!err)
		{
			int32 length;
			BOOL relative = FALSE;
			LPWSTR start = NULL;

			switch (buffer->ReparseTag)
			{
				case IO_REPARSE_TAG_SYMLINK:
					start = (LPWSTR)((char*)buffer->SymbolicLinkReparseBuffer.PathBuffer + buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset);
					length = buffer->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
					relative = buffer->SymbolicLinkReparseBuffer.Flags & SYMLINK_FLAG_RELATIVE;
					break;
				case IO_REPARSE_TAG_MOUNT_POINT:
					start = (LPWSTR)((char*)buffer->MountPointReparseBuffer.PathBuffer + buffer->MountPointReparseBuffer.SubstituteNameOffset);
					length = buffer->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
					break;
				default:
					length = 0;
					err = mgArgErr;
					break;
			}
			if (length)
			{
				int32 parentLen = 0, newLen = length,
					  offset = HasDOSDevicePrefix(start, length);

				if (resolveCount)
					(*resolveCount)++;

				switch (offset)
				{
					case 0:
						/* No DOS device prefix, if it is an UNC name allocate 6 extra bytes and copy from the second character */
						if (length >= 2 && start[0] == kPathSeperator && start[1] == kPathSeperator)
						{
							relative = FALSE;
							offset = 1;
							newLen += 6;
						}
						/* fall through */
					case 4:
						/* If absolute path with drive specification */
						if (length - offset >= 3 && start[offset] < 128 && isalpha(start[offset]) && start[offset + 1] == ':' && start[offset + 2] == kPathSeperator)
						{
							relative = FALSE;
							newLen += (4 - offset);
						}
						break;
					case 8:
						/* DOS device prefix with UNC extension */
						relative = FALSE;
						offset = 7;
						break;
					default:
						err = mgArgErr;
						break;
				}

				if (relative)
				{
					parentLen = LWPathParent(wSrc, -1);
					if (length && start[0] != kPathSeperator)
						parentLen++;
					newLen = parentLen + length + 1;
				}

				if (relative)
				{
					if (start[0] == kPathSeperator)
						 offset = 1;
					err = LWPathNCat(&wIntermediate, 0, LWPathBuf(wIntermediate), parentLen);
				}
				else if (offset == 1 || offset == 7)
				{
					err = LWPathNCat(&wIntermediate, 0, L"\\\\?\\UNC", 7);
				}
				else
				{
					err = LWPathNCat(&wIntermediate, 0, L"\\\\?\\", 4);
				}

				if (!err)
					err = LWPathNCat(&wIntermediate, -1, start + offset, length - offset);
					
				if (!err)
				{
					*dwAttrs = GetFileAttributesLW(wIntermediate);
					if (*dwAttrs == INVALID_FILE_ATTRIBUTES)
					{
						err = Win32GetLVFileErr();
					}
					else if (!resolveCount || (resolveDepth > 0 && resolveDepth <= *resolveCount) || !(*dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT))
					{
						if (relative && resolveDepth <= 0)
							err = LWPathNCat(&wIntermediate, 0, start, length);
						break;
					}
				}
			}
			else
				err = fIOErr;
		}
		wSrc = wIntermediate;
	} while (!err);

	Win32ModifyBackupPrivilege(FALSE);
	DSDisposePtr((UPtr)buffer);
	if (!err)
	{
		if (wTgt)
		{
			*wTgt = wIntermediate;
			return noErr;
		}
	}
	LWPathDispose(wIntermediate);
	return err;
}
#endif
#elif Unix || MacOSX
/* seconds between Jan 1 1904 GMT and Jan 1 1970 GMT */
#define dt1970re1904    2082844800L

#if VxWorks
// on VxWorks the stat time values are unsigned long integer
static void VxWorksConvertFromATime(ATime128 *time, unsigned long *sTime)
{
	/* VxWorks uses unsignde integers and can't represent dates before Jan 1, 1970 */
	if (time->u.f.val > dt1970re1904)
		*sTime = (unsigned long)(time->u.f.val - dt1970re1904);
	else
		*sTime = 0;
} 

static void VxWorksConvertToATime(unsigned long sTime, ATime128 *time)
{
	time->u.f.val = (int64_t)sTime + dt1970re1904;
	time->u.f.fract = 0;
}
#else
// on Mac/Linux kernel 2.6 and newer the stat time values are struct timespec values
static void UnixConvertFromATime(ATime128 *time, struct timespec *sTime)
{
	/* The LabVIEW default default value is used to indicate to not update this value */
	if (time->u.f.val || time->u.f.fract)
	{
		sTime->tv_sec = (time_t)(time->u.f.val - dt1970re1904);
		sTime->tv_nsec = (int32_t)(time->u.p.fractHi / 4.294967296);
	}
	else
	{
		sTime->tv_nsec = UTIME_OMIT;
	}
}

static void UnixConvertToATime(struct timespec *sTime, ATime128 *time)
{
	time->u.f.val = (int64_t)sTime->tv_sec + dt1970re1904;
	time->u.p.fractHi = (uint32_t)sTime->tv_nsec * 4.294967296;
	time->u.p.fractLo = 0;
}
#endif

static MgErr UnixToLVFileErr(void)
{
    switch (errno)
    {
      case 0:           return mgNoErr;
      case ESPIPE:      return fEOF;
      case EINVAL:
      case EBADF:       return mgArgErr;
      case ETXTBSY:     return fIsOpen;
      case ENOENT:      return fNotFound;
#ifdef EAGAIN
      case EAGAIN:  /* SVR4, file is locked */
#endif
#ifdef EDEADLK
      case EDEADLK: /* deadlock would occur */
#endif
#ifdef ENOLCK
      case ENOLCK:  /* NFS, lock not avail */
#endif
      case EPERM:
      case EACCES:      return fNoPerm;
      case ENOSPC:      return fDiskFull;
      case EEXIST:      return fDupPath;
      case ENFILE:
      case EMFILE:      return fTMFOpen;
      case ENOMEM:      return mFullErr;
      case EIO:         return fIOErr;
    }
    return fIOErr;   /* fIOErr generally signifies some unknown file error */
}

static uInt16 LVFileTypeFromStat(struct stat *statbuf)
{
	uInt16 flags = 0;
	switch (statbuf->st_mode & S_IFMT)
	{
		case S_IFREG:
			flags = kIsFile;
			break;
		case S_IFLNK:
			flags = kIsLink;
			break;
		case S_IFDIR:
			flags = 0;
			break;
	}

#if MacOSX
	if (statbuf->st_flags & UF_HIDDEN)
        flags |= kFIsInvisible;

	if (statbuf->st_flags & UF_COMPRESSED)
		flags |= kIsCompressed;
#endif
	return flags;
}

#if MacOSX
typedef struct
{
    uint32_t     length;
    off_t        size;
} FSizeAttrBuf;

static MgErr MacGetResourceSize(const char *path, uInt64 *size)
{
    int          err;
    attrlist_t   attrList;
    FSizeAttrBuf attrBuf;

    memset(&attrList, 0, sizeof(attrList));
    attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrList.fileattr  = ATTR_FILE_RSRCLENGTH;

	if (getattrlist(path, &attrList, &attrBuf, sizeof(attrBuf), FSOPT_NOFOLLOW)}
		return UnixToLVFileErr();

	assert(attrBuf.length == sizeof(attrBuf));
	*size = attrBuf.size;
	return noErr;
}
#endif
#endif

/* Internal list directory function
   On Windows the pathName is a wide char Long String pointer and the function returns UTF8 encoded filenames in the name array
   On other platforms it uses whatever is the default encoding for both pathName and the filenames, which could be UTF8 (Linux and Mac)
 */
static MgErr lvFile_ListDirectory(LWPathHandle pathName, LStrArrHdl *nameArr, FileInfoArrHdl *typeArr, uInt32 cp, int32 resolveDepth)
{
	MgErr err;
	int32 rootLength = 0, index = 0, size = 8;
	LWChar *path = LWPathBuf(pathName);
#if Win32
	HANDLE dirp = INVALID_HANDLE_VALUE;
	DWORD dwAttrs;
#if Pharlap
	LPSTR fileName = NULL;
	WIN32_FIND_DATAA fileData;
#else
	PVOID oldRedirection = NULL;
	LWPathHandle wTgt = NULL;
	DWORD dwAttrsResolved;
	LPWSTR fileName = NULL;
	WIN32_FIND_DATAW fileData;
#endif
#else // Unix, VxWorks, and MacOSX
    struct stat statbuf;
	DIR *dirp;
	struct dirent *dp;
#if !VxWorks    /* no links */
    struct stat tmpstatbuf;
#endif
#endif

	err = NumericArrayResize(uPtr, 1, (UHandle*)nameArr, size);
	if (err)
		return err;

	err = NumericArrayResize(uL, 1, (UHandle*)typeArr, size);
	if (err)
		return err;

#if Win32
	if (!LWPathLenGet(pathName))
	{
		DWORD drives = GetLogicalDrives();
		uChar drive = 'A';
	    for (; !err && drive <= 'Z'; drive++)
		{
            if (drives & 01)
		    {
				if (index >= size)
				{
					size *= 2;
					err = NumericArrayResize(uPtr, 1, (UHandle*)nameArr, size);
					if (err)
						return err;

					err = NumericArrayResize(uL, 1, (UHandle*)typeArr, size);
					if (err)
						return err;
				}
				(**typeArr)->elm[index] = 0;

				err = NumericArrayResize(uB, 1, (UHandle*)((**nameArr)->elm + index), 3);
				if (!err)
				{
					LStrBuf(*((**nameArr)->elm[index]))[0] = drive;
					LStrBuf(*((**nameArr)->elm[index]))[1] = ':';
					LStrBuf(*((**nameArr)->elm[index]))[2] = kPathSeperator;
					LStrLen(*((**nameArr)->elm[index])) = 3;
				}
				index++;
				(**nameArr)->numItems = index;
				(**typeArr)->numItems = index;
		    }
	        drives >>= 1;
		}
		return err;
	}

	/* Check that we have actually a folder */
	dwAttrs = GetFileAttributesLW(pathName);
	if (dwAttrs == INVALID_FILE_ATTRIBUTES)
		return Win32GetLVFileErr();

	if (!(dwAttrs & FILE_ATTRIBUTE_DIRECTORY))
		return mgArgErr;

	err = LWPathAppendSeparator(pathName);
	if (err)
		goto FListDirOut;

	rootLength = LWPathLenGet(pathName);
	err = LWPathNCat(&pathName, rootLength, strWildcardPattern, 3);
	if (err)
		goto FListDirOut;

#if !Pharlap
	if (!Wow64DisableWow64FsRedirection(&oldRedirection))
	{
		DWORD ret = GetLastError();
		/* Failed but lets go on anyhow, risking strange results for virtualized paths */
		oldRedirection = NULL;
	}
#endif
	path = LWPathBuf(pathName);
	dirp = FindFirstFileW(path, &fileData);
	if (dirp == INVALID_HANDLE_VALUE)
	{
		DWORD ret = GetLastError();
		if (ret != ERROR_FILE_NOT_FOUND)
			err = Win32ToLVFileErr(ret);
		(**typeArr)->numItems = 0;
		goto FListDirOut;
	}

	do
	{
		/* Skip the current dir, and parent dir entries */
		if (fileData.cFileName[0] != 0 && (fileData.cFileName[0] != '.' || (fileData.cFileName[1] != 0 && (fileData.cFileName[1] != '.' || fileData.cFileName[2] != 0))))
		{
			/* Make sure our arrays are resized to allow the new values */
			if (index >= size)
			{
				size *= 2;
				err = NumericArrayResize(uPtr, 1, (UHandle*)nameArr, size);
				if (err)
					goto FListDirOut;

				err = NumericArrayResize(uL, 1, (UHandle*)typeArr, size);
				if (err)
					goto FListDirOut;
			}

			(**typeArr)->elm[index] = 0;
			fileName = fileData.cFileName;

			dwAttrs = fileData.dwFileAttributes;
			if (dwAttrs != INVALID_FILE_ATTRIBUTES)
			{
#if Pharlap
                (**typeArr)->elm[index] = LVFileTypeFromWinFlags(dwAttrs);
#else
				/* Create the path to the file for intermediate operations */
				err = LWPathNCat(&pathName, rootLength, fileName, -1);
				if (err)
					goto FListDirOut;

				err = lvFile_ReadLink(pathName, &wTgt, resolveDepth, NULL, &((**typeArr)->elm[index]));
#endif
			}
			else
			{
				/* can happen if file disappeared since we did FindNextFile */
				(**typeArr)->elm[index] |= kErrGettingType | kIsFile;
			}
#if Pharlap
			err = ConvertCString(fileName, -1, CP_ACP, (**nameArr)->elm + index, cp, 0, NULL);
#else
			err = WideCStrToMultiByte(fileName, -1, (**nameArr)->elm + index, cp, 0, NULL);
#endif
			index++;
			(**nameArr)->numItems = index;
			(**typeArr)->numItems = index;
		}
	}
	while (FindNextFileLW(dirp, &fileData));

	if (!err)
		err = Win32GetLVFileErr();

FListDirOut:
#if !Pharlap
	LWPathDispose(wTgt);
	if (oldRedirection)
		Wow64RevertWow64FsRedirection(&oldRedirection);
#endif
	if (dirp != INVALID_HANDLE_VALUE)
		FindClose(dirp);
#else
	if (!LWPathLenGet(pathName))
		path = "/";

	/* Check that we have actually a folder */
    if (lstat(path, &statbuf))
		return UnixToLVFileErr();

	if (!S_ISDIR(statbuf.st_mode))
		return mgArgErr;

	err = NumericArrayResize(uPtr, 1, (UHandle*)nameArr, size);
	if (err)
		return err;

	err = NumericArrayResize(uL, 1, (UHandle*)typeArr, size);
	if (err)
		return err;

	if (!(dirp = opendir(path))))
		return UnixToLVFileErr();

	err = LWPathAppendSeparator(pathName);
	if (err)
		goto FListDirOut;

	rootLength = LWPathLenGet(pathName);

	for (dp = readdir(dirp); dp; dp = readdir(dirp))
	{
		/* Skip the current dir, and parent dir entries. They are not guaranteed to be always the first
		   two entries enumerated! */
		if (dp->d_name[0] != '.' || (dp->d_name[1] != 0 && (dp->d_name[1] != '.' || dp->d_name[2] != 0)))
		{
			if (index >= size)
			{
				size *= 2;
				err = NumericArrayResize(uPtr, 1, (UHandle*)nameArr, size);
				if (err)
			        goto FListDirOut;

				err = NumericArrayResize(uL, 1, (UHandle*)typeArr, size);
				if (err)
			        goto FListDirOut;
			}

			(**typeArr)->elm[index] = 0;

			err = ConvertCString((ConstCStr)dp->d_name, -1, CP_ACP, (**nameArr)->elm + index, cp, 0, NULL);
			if (err)
			    goto FListDirOut;
	
			err = LWPathNCat(&pathName, rootLength, dp->d_name, -1);
			if (err)
			    goto FListDirOut;

			path = LWPathBuf(pathName);
			if (lstat(path), &statbuf))
			{
				(**typeArr)->elm[index] |= kErrGettingType;
			}
			else
			{
#if !VxWorks    /* VxWorks does not support links */
				if (S_ISLNK(statbuf.st_mode))
				{
					(**typeArr)->elm[index] |= kIsLink;
					if (stat(path), &tmpstatbuf) == 0)	/* If link points to something */
						statbuf = tmpstatbuf;				        /* return info about it not link. */
				}
#endif
				if (!S_ISDIR(statbuf.st_mode))
				{
					(**typeArr)->elm[index] |= kIsFile;
				}
			}
			index++;
			(**nameArr)->numItems = index;
			(**typeArr)->numItems = index;
		}
	}
FListDirOut:
	closedir(dirp);
#endif
	if (index < size)
	{
		if (*nameArr || index > 0)
			NumericArrayResize(uPtr, 1, (UHandle*)nameArr, index);
		if (*typeArr || index > 0)
			NumericArrayResize(uL, 1, (UHandle*)typeArr, index);
	}
	if (!err)
		LWPathLenSet(pathName, rootLength - 1);
	return err;
}

LibAPI(MgErr) LVFile_ListDirectory(LWPathHandle *folderPath, LStrArrHdl *nameArr, FileInfoArrHdl *typeArr, int32 resolveDepth)
{
	MgErr err = LWPathZeroTerminate(*folderPath, NULL);
	if (!err)
		err = lvFile_ListDirectory(*folderPath, nameArr, typeArr, CP_UTF8, resolveDepth);
	return err;
}

/* On Windows and Mac, folderPath is a UTF8 encoded string, on other platforms it is locally encoded but
   this could be UTF8 (Linux, depending on its configuration) */
LibAPI(MgErr) LVString_ListDirectory(LStrHandle folderPath, LStrArrHdl *nameArr, FileInfoArrHdl *typeArr, int32 resolveDepth)
{
	LWPathHandle pathName = NULL;
	MgErr err = LStrToLWPath(folderPath, CP_UTF8, &pathName, kDefaultPath, 280);
	if (!err)
	{
		err = lvFile_ListDirectory(pathName, nameArr, typeArr, CP_UTF8, resolveDepth);
		LWPathDispose(pathName);
	}
	return err;
}

/* This is the LabVIEW Path version of above function. It's strings are always locally encoded so there
   can be a problem with paths on Windows and other systems that don't use UTF8 as local encoding */
LibAPI(MgErr) LVPath_ListDirectory(Path folderPath, LStrArrHdl *nameArr, FileInfoArrHdl *typeArr, int32 resolveDepth)
{
	LWPathHandle pathName = NULL;
	MgErr err = LPathToLWPath(folderPath, &pathName, kDefaultPath, 280);
	if (!err)
	{
		err = lvFile_ListDirectory(pathName, nameArr, typeArr, CP_ACP, resolveDepth);
		LWPathDispose(pathName);
	}
	return err;
}

static MgErr lvFile_HasResourceFork(LWPathHandle pathName, LVBoolean *hasResFork, FileOffset *size)
{
    MgErr  err = mgNoErr;
#if !MacOSX
    Unused(pathName);
#endif
    if (hasResFork)
	    *hasResFork = 0;
	if (size)
		size->q = 0;

#if MacOSX
	FileOffset offset;
	err = MacGetResourceSize(LWPathBuf(lwstr), &offset.q);
	if (!err)
	{
        if (hasResFork)
            *hasResFork = LV_TRUE;
        if (size)
            size->q = offset.q;
    }
#endif
    return err;
}

LibAPI(MgErr) LVString_HasResourceFork(LStrHandle pathName, LVBoolean *hasResFork, FileOffset *size)
{
    LWPathHandle lwstr = NULL;
    MgErr err = LStrToLWPath(pathName, CP_UTF8, &lwstr, kDefaultPath, 4);
    if (!err)
    {
		err = lvFile_HasResourceFork(lwstr, hasResFork, size);
		LWPathDispose(lwstr);
	}
	return err;
}

LibAPI(MgErr) LVPath_HasResourceFork(Path pathName, LVBoolean *hasResFork, FileOffset *size)
{
    LWPathHandle lwstr = NULL;
    MgErr err = LPathToLWPath(pathName, &lwstr, 0, 0);
    if (!err)
    {
		err = lvFile_HasResourceFork(lwstr, hasResFork, size);
		LWPathDispose(lwstr);
	}
	return err;
}

#if MacOSX
#ifndef UTIME_NOW
#define	UTIME_NOW	-1
#define	UTIME_OMIT	-2
#endif
static int lutimens(const char *path, struct timespec times_in[3])
{

    size_t attrbuf_size = 0;
    struct timespec times_out[3] = {};
    struct attrlist a = {
		.commonattr = 0;
        .bitmapcount = ATTR_BIT_MAP_COUNT
    };
    struct timespec *times_cursor = times_out;
    if (times_in[2].tv_nsec != UTIME_OMIT)
    {
        a.commonattr |= ATTR_CMN_CRTIME;
        *times_cursor++ = times_in[2];
        attrbuf_size += sizeof(struct timespec);
    }
    if (times_in[1].tv_nsec != UTIME_OMIT)
    {
        a.commonattr |= ATTR_CMN_MODTIME;
        *times_cursor++ = times_in[1];
        attrbuf_size += sizeof(struct timespec);
    }
    if (times_in[0].tv_nsec != UTIME_OMIT)
    {
        a.commonattr |= ATTR_CMN_ACCTIME;
        *times_cursor = times_in[0];
        attrbuf_size += sizeof(struct timespec);
    }
    return setattrlist(path, &a, &times_out, attrbuf_size, FSOPT_NOFOLLOW);
}
#endif

#define kSetableWinFileFlags kWinFileInfoArchive | kWinFileInfoHidden | kWinFileInfoNotIndexed | \
				             kWinFileInfoOffline | kWinFileInfoReadOnly | kWinFileInfoSystem | kWinFileInfoTemporary

static uInt16 UnixFlagsFromWindows(uInt16 attr)
{
	uInt16 flags = attr & kWinFileInfoReadOnly ?  0444 : 0666;
	if (attr & kWinFileInfoDirectory)
		flags |= 040000;
	else if (attr & kWinFileInfoDevice)
		flags |= 060000;
	else if (attr & kWinFileInfoReparsePoint)
		flags |= 0120000;
	else
		flags |= 010000; 
	return flags;
}

static uInt16 WinFlagsFromUnix(uInt32 mode)
{
	uInt16 flags = mode & 0222 ? 0 : kWinFileInfoReadOnly;
	switch (mode & 0170000)
	{
	     case 040000:
			flags |= kWinFileInfoDirectory;
			break;
	     case 020000:
	     case 060000:
			flags |= kWinFileInfoDevice;
			break;
	     case 0120000:
			flags |= kWinFileInfoReparsePoint;
			break;
	}
	if (!flags)
		flags = kWinFileInfoNormal;
	return flags;
}

static uInt32 MacFlagsFromWindows(uInt16 attr)
{
	uInt32 flags = 0;
	if (!(attr & kWinFileInfoDirectory) && (attr & kWinFileInfoReadOnly))
		flags |= kMacFileInfoImmutable;
	if (attr & kWinFileInfoHidden)
		flags |= kMacFileInfoHidden;
	if (attr & kWinFileInfoCompressed)
		flags |= kMacFileInfoCompressed;
	if (!(attr & kWinFileInfoArchive))
		flags |= kMacFileInfoNoDump;
	return flags;
}

static MgErr lvFile_FileInfo(LWPathHandle pathName, uInt8 write, LVFileInfo *fileInfo)
{
	MgErr err = noErr;
	uInt8 type = LWPathTypeGet(pathName);
	uInt16 cnt = LWPathCntGet(pathName);
#if Win32 // Windows
    HANDLE handle = NULL;
	uInt64 count = 0;
#if Pharlap
	WIN32_FIND_DATAA fi = {0};
#else
	WIN32_FIND_DATAW fi = {0};
#endif
#else // Unix, VxWorks, and MacOSX
    struct stat statbuf;
	uInt64 count = 0;
#if VxWorks
    struct utimbuf times;
#else
	struct timespec times[3];
#endif
#endif

	if (type != fAbsPath && type != fUNCPath)
		return mgArgErr;

#if Win32
	/* FindFirstFile fails with empty path (desktop) or volume letter alone */
    if (cnt <= 1)
    {
		fi.ftCreationTime.dwLowDateTime = fi.ftCreationTime.dwHighDateTime = 0;
		fi.ftLastWriteTime.dwLowDateTime = fi.ftLastWriteTime.dwHighDateTime = 0;
		fi.ftLastAccessTime.dwLowDateTime = fi.ftLastAccessTime.dwHighDateTime = 0;
		fi.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
		handle = FindFirstFileLW(pathName, &fi);
		if (handle == INVALID_HANDLE_VALUE)
			err = Win32GetLVFileErr();
		else
		{
			if (!FindClose(handle)) 
				err = Win32GetLVFileErr();
		}
    }

    if (!err)
    {
		if (write)
		{
			ATimeToFileTime(&fileInfo->cDate, &fi.ftCreationTime);
			ATimeToFileTime(&fileInfo->mDate, &fi.ftLastWriteTime);
			ATimeToFileTime(&fileInfo->aDate, &fi.ftLastAccessTime);
			handle = CreateFileLW(pathName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
			if (handle != INVALID_HANDLE_VALUE)
			{
			    if (!SetFileTime(handle, &fi.ftCreationTime, &fi.ftLastAccessTime, &fi.ftLastWriteTime))
				    err = Win32GetLVFileErr();
				CloseHandle(handle);
			}
			else
				err = Win32GetLVFileErr();

			if (!err)
			{
				fi.dwFileAttributes = GetFileAttributesLW(pathName);
				if (fi.dwFileAttributes != INVALID_FILE_ATTRIBUTES)
			    {
					if (!fileInfo->winFlags && fileInfo->unixFlags)
							fileInfo->winFlags = WinFlagsFromUnix(fileInfo->unixFlags);

					fi.dwFileAttributes = (fi.dwFileAttributes & ~kSetableWinFileFlags) | (fileInfo->winFlags & kSetableWinFileFlags);
                    if (!fi.dwFileAttributes)
						fi.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

				    SetFileAttributesLW(pathName, fi.dwFileAttributes);
			    }
			}
		}
		else
		{
			fileInfo->uid = 0xFFFFFFFF;
			fileInfo->gid = 0xFFFFFFFF;
			FileTimeToATime(&fi.ftCreationTime, &fileInfo->cDate);
			FileTimeToATime(&fi.ftLastWriteTime, &fileInfo->mDate);
			FileTimeToATime(&fi.ftLastAccessTime, &fileInfo->aDate);
			fileInfo->rfSize = 0;
			fileInfo->winFlags = Lo16(fi.dwFileAttributes);
			fileInfo->unixFlags = UnixFlagsFromWindows(fileInfo->winFlags);
			fileInfo->macFlags = MacFlagsFromWindows(fileInfo->winFlags);
			fileInfo->fileType = LVFileTypeFromWinFlags(fi.dwFileAttributes);

			if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				int32 len = LWPathLenGet(pathName);

				fileInfo->type = kUnknownFileType;
				fileInfo->creator = kUnknownCreator;

				if (!len)
				{
					DWORD drives = GetLogicalDrives();
					int drive;

	                count = 0;
	                for (drive = 1; drive <= 26; drive++)
					{
                        if (drives & 01)
		                {
                            count++;
			            }
	                    drives >>= 1;
		            }
				}
				else
				{
				    count = 1;

					err = LWPathAppendSeparator(pathName);
					if (!err)
					{
				        err = LWPathNCat(&pathName, -1, strWildcardPattern, 3);
						if (!err)
						{
							handle = FindFirstFileLW(pathName, &fi);
							if (handle == INVALID_HANDLE_VALUE)
								count = 0;
							else
								while (FindNextFileLW(handle, &fi))
									count++;
							FindClose(handle);
						}
						LWPathZeroTerminate(pathName, LWPathBuf(pathName) + len);
					}
				}
	            /* FindFirstFile doesn't enumerate . and .. entries in a volume root */
				if (cnt <= 1)
				    fileInfo->size = count;
				else
				    fileInfo->size = count - 2;
			}
			else
			{		
#if !Pharlap
				err = Win32ResolveShortCut(pathName, NULL, 0, NULL, &fi.dwFileAttributes);
				if (!err)
					fileInfo->fileType |= kIsLink;
				else if (err == cancelError)
					err = mgNoErr;
#endif
				LWPathGetFileTypeAndCreator(pathName, &fileInfo->type, &fileInfo->creator);
				if (fileInfo->type && fileInfo->type != kUnknownFileType)
					fileInfo->fileType |= kRecognizedType;
				fileInfo->size = Quad(fi.nFileSizeHigh, fi.nFileSizeLow);
			}
		}
    }
#else
    if (lstat(LWPathBuf(pathName), &statbuf))
		return UnixToLVFileErr();

	if (write)
	{
		if (fileInfo->winFlags)
		{
			if (!fileInfo->unixFlags)
				fileInfo->unixFlags = UnixFlagsFromWindows(fileInfo->winFlags);
			if (!fileInfo->macFlags)
				fileInfo->macFlags = MacFlagsFromWindows(fileInfo->winFlags);
		}
#if VxWorks
		VxWorksConvertFromATime(&fileInfo->aDate, &times.actime);
		VxWorksConvertFromATime(&fileInfo->mDate, &times.modtime);
        VxWorksConvertFromATime(&fileInfo->cDate, &statbuf.st_ctime);
		if (utime(LWPathBuf(pathName), &times))
#else
		UnixConvertFromATime(&fileInfo->aDate, &times[0]);
		UnixConvertFromATime(&fileInfo->mDate, &times[1]);
        UnixConvertFromATime(&fileInfo->cDate, &times[2]);
		if (lutimens(LWPathBuf(pathName)), times))
#endif
			err = UnixToLVFileErr();
#if !VxWorks
		/*
		 * Changing the ownership probably won't succeed, unless we're root
         * or POSIX_CHOWN_RESTRICTED is not set.  Set uid/gid before setting
         * the mode; current BSD behavior is to remove all setuid bits on
         * chown. If chown fails, loose setuid/setgid bits.
         */
        else if (chown(LWPathBuf(pathName), fileInfo->uid, fileInfo->gid))
		{
	        if (errno != EPERM && errno != ENOTSUP)
				err = UnixToLVFileErr();
	        fileInfo->unixFlags &= ~(S_ISUID | S_ISGID);
        }
#endif
        if (!err && chmod(LWPathBuf(pathName), (statbuf.st_mode & 0170000) | (fileInfo->unixFlags & 07777)) && errno != ENOTSUP)
			err = UnixToLVFileErr();
#if MacOSX
		else if (fileInfo->macFlags && lchflags(buf, fileInfo->macFlags))
			err = UnixToLVFileErr();			
#endif
	}
	else
	{
		fileInfo->uid = statbuf.st_uid;
		fileInfo->gid = statbuf.st_gid;
#if VxWorks
		VxWorksConvertToATime(statbuf.st_ctime, &fileInfo->cDate);
		VxWorksConvertToATime(statbuf.st_atime, &fileInfo->aDate);
		VxWorksConvertToATime(statbuf.st_mtime, &fileInfo->mDate);
#else
#if MacOSX
		UnixConvertToATime(&statbuf.st_birthtimespec, &fileInfo->cDate);
#endif
		UnixConvertToATime(&statbuf.st_mtimespec, &fileInfo->mDate);
		UnixConvertToATime(&statbuf.st_atimespec, &fileInfo->aDate);
#endif
		fileInfo->unixFlags = Lo16(statbuf.st_mode);
		fileInfo->winFlags = WinFlagsFromUnix(fileInfo->unixFlags);
#if MacOSX
		fileInfo->macFlags = statbuf.st_flags;
#else
		fileInfo->macFlags = 0;
#endif
		fileInfo->fileType = LVFileTypeFromStat(&statbuf);

		if (S_ISDIR(statbuf.st_mode))
		{
			DIR *dirp;
			struct dirent *dp;

			fileInfo->type = kUnknownFileType;
			fileInfo->creator = kUnknownCreator;

			if (!(dirp = opendir(LWPathBuf(pathName))))
				return UnixToLVFileErr();

			for (dp = readdir(dirp); dp; dp = readdir(dirp))
				count++;
			closedir(dirp);
			fileInfo->size = count - 2;
			fileInfo->rfSize = 0;
		}
		else
		{
			/* Try to determine LabVIEW file types based on file ending? */
			err = LWPathGetFileTypeAndCreator(pathName, &fileInfo->type, &fileInfo->creator);
			if (fileInfo->type && fileInfo->type != kUnknownFileType)
				fileInfo->fileType |= kRecognizedType;

			fileInfo->size = statbuf.st_size;
#if MacOSX
			MacGetResourceSize(LWPathBuf(pathName), &fileInfo->rfSize);
#else
			fileInfo->rfSize = 0;
#endif
		}
	}
#endif
    return err;
}

LibAPI(MgErr) LVFile_FileInfo(LWPathHandle *pathName, uInt8 write, LVFileInfo *fileInfo)
{
	MgErr err = LWPathZeroTerminate(*pathName, NULL);
	if (!err)
		err = lvFile_FileInfo(*pathName, write, fileInfo);
	return err;
}

LibAPI(MgErr) LVString_FileInfo(LStrHandle path, uInt8 write, LVFileInfo *fileInfo)
{
    MgErr err = mgNoErr;
    LWPathHandle pathName = NULL;

	if (LStrLenH(path))
	{
		err = LStrToLWPath(path, CP_UTF8, &pathName, kDefaultPath, 4);
	}
    if (!err)
	{
		err = lvFile_FileInfo(pathName, write, fileInfo);
		LWPathDispose(pathName);
	}
	return err;
}

LibAPI(MgErr) LVPath_FileInfo(Path path, uInt8 write, LVFileInfo *fileInfo)
{
    MgErr err = mgNoErr;
    LWPathHandle pathName = NULL;

	if (!FIsEmptyPath(path))
	{
		err = LPathToLWPath(path, &pathName, kDefaultPath, 4);
	}
	if (!err)
	{
		err = lvFile_FileInfo(pathName, write, fileInfo);
		LWPathDispose(pathName);
	}
	return err;
}

/* 
   These two APIs will use the platform specific path syntax except for the
   MacOSX 32-bit plaform where it will use posix format
*/
LibAPI(MgErr) LVPath_ToText(Path path, LVBoolean isUtf8, LStrHandle *str)
{
	int32 pathLen = -1;
	MgErr err = FPathToText(path, (LStrPtr)&pathLen);
	if (!err)
	{
		err = NumericArrayResize(uB, 1, (UHandle*)str, pathLen + 1);
		if (!err)
		{
			LStrLen(**str) = pathLen;
			err = FPathToText(path, **str);
			if (!err)
			{
#if usesHFSPath
				err = ConvertToPosixPath(*str, CP_ACP, str, isUtf8 ? CP_UTF8 : CP_ACP, '?', NULL, false);
#else
				if (isUtf8)
					err = ConvertLString(*str, CP_ACP, str, isUtf8 ? CP_UTF8 : CP_ACP, '?', NULL);

#endif
				if (!err && LStrBuf(**str)[LStrLen(**str) - 1] == kPathSeperator)
				{
					LStrLen(**str)--;
				}
			}
		}
	}
	return err;
}

LibAPI(MgErr) LVPath_FromText(CStr str, int32 len, Path *path, LVBoolean isDir)
{
	MgErr err = mgNoErr;
#if usesHFSPath
	LStrHandle hfsPath = NULL;
	/* Convert the posix path to an HFS path */
	err = ConvertFromPosixPath(str, len, CP_ACP, &hfsPath, CP_ACP, '?', NULL, isDir);
	if (!err && hfsPath)
	{
		err = FTextToPath(LStrBuf(*hfsPath), LStrLen(*hfsPath), path);
	}
#else
	Unused(isDir);
	err = FTextToPath(str, len, path);
#endif
	return err;
}

#if Win32 && !Pharlap
typedef BOOL (WINAPI *tCreateHardLink)(LPCWSTR lpFileName, LPCWSTR lpExistingFileName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
static MgErr Win32CreateHardLink(LWPathHandle lwFileName, LWPathHandle lwExistingFileName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
	MgErr err = mgNotSupported;
	static tCreateHardLink pCreateHardLink = NULL;
	if (!pCreateHardLink)
	{
		HMODULE hLib = LoadLibrary("kernel32.dll");
		if (hLib)
		{
			pCreateHardLink = (tCreateHardLink)GetProcAddress(hLib, "CreateHardLinkW");
		}
	}
	if (pCreateHardLink)
	{
		if (!pCreateHardLink(LWPathBuf(lwFileName), LWPathBuf(lwExistingFileName), lpSecurityAttributes))
			err = Win32GetLVFileErr();
		else
			err = noErr;
	}
	return err;
}

#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY (0x1)
#endif

static BOOL AcquireSymlinkPriv(void)
{
	HANDLE hToken;
	TOKEN_PRIVILEGES TokenPriv;
	BOOL result;

	if (!LookupPrivilegeValue(NULL, SE_CREATE_SYMBOLIC_LINK_NAME, &TokenPriv.Privileges[0].Luid))
		// This privilege does not exist before Windows XP
		return TRUE;

	TokenPriv.PrivilegeCount = 1;
	TokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
		return FALSE;

	result = AdjustTokenPrivileges(hToken, FALSE, &TokenPriv, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
	CloseHandle(hToken);

	return result;
}

typedef BOOL (WINAPI *tCreateSymbolicLink)(LPCWSTR lpSymlinkFileName, LPCWSTR lpTargetFileName, DWORD dwFlags);

static MgErr Win32CreateSymbolicLink(LWPathHandle lwSymlinkFileName, LWPathHandle lwTargetFileName, LPSECURITY_ATTRIBUTES lpsa, DWORD dwFlags)
{
	MgErr err = mgNotSupported;
	static tCreateSymbolicLink pCreateSymbolicLink = NULL;
	if (!(dwFlags & 0x8000) && !pCreateSymbolicLink)
	{
		HMODULE hLib = LoadLibrary("kernel32.dll");
		if (hLib)
		{
			pCreateSymbolicLink = (tCreateSymbolicLink)GetProcAddress(hLib, "CreateSymbolicLinkW");
		}
	}
	if (!(dwFlags & 0x8000) && pCreateSymbolicLink)
	{
		if (!pCreateSymbolicLink(LWPathBuf(lwSymlinkFileName), LWPathBuf(lwTargetFileName), dwFlags))
			err = Win32GetLVFileErr();
		else
			err = noErr;
	}
	else
	{
		BOOL isRelative = TRUE, isDirectory = FALSE;
		HANDLE hFile;
		BOOL (WINAPI *deletefunc)();
		DWORD attr = GetFileAttributesLW(lwTargetFileName);
		if (attr != INVALID_FILE_ATTRIBUTES)
			isDirectory = (attr & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
		else
			isDirectory = (dwFlags & SYMBOLIC_LINK_FLAG_DIRECTORY) == SYMBOLIC_LINK_FLAG_DIRECTORY;

	    if (isDirectory)
		{
		    if (!CreateDirectoryLW(lwSymlinkFileName, lpsa))
			{
				return Win32GetLVFileErr();
			}
			hFile = CreateFileLW(lwSymlinkFileName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, lpsa, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS || FILE_FLAG_OPEN_REPARSE_POINT, NULL);
			deletefunc = RemoveDirectoryW;
		}
		else
		{
			hFile = CreateFileLW(lwSymlinkFileName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, lpsa, CREATE_NEW, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
			deletefunc = DeleteFileW;
	    }
		if (hFile != INVALID_HANDLE_VALUE)
		{
			int32 length = LWPathLenGet(lwTargetFileName) + 1;
			LPWSTR lpTargetFileName = LWPathBuf(lwTargetFileName);
			WCHAR namebuf[MAX_PATH + 6];
			DWORD bytes = (DWORD)(REPARSE_DATA_BUFFER_HEADER_SIZE + length * sizeof(WCHAR) * 2 + 20);
			PREPARSE_DATA_BUFFER buffer = (PREPARSE_DATA_BUFFER)DSNewPClr(bytes);
			if (buffer)
			{
				int32 offset = HasDOSDevicePrefix(lpTargetFileName, length);
				switch (offset)
				{
					case 0:
						if (length >= 3 && lpTargetFileName[0] == kPathSeperator && lpTargetFileName[1] == kPathSeperator && lpTargetFileName[2] < 128 && isalpha(lpTargetFileName[2]))
						{
							isRelative = FALSE;
						}
					case 4:
						if (length >= 3 && lpTargetFileName[offset] < 128 && isalpha(lpTargetFileName[offset]) && lpTargetFileName[offset + 1] == ':' && lpTargetFileName[offset + 2] == kPathSeperator)
						{
							isRelative = FALSE;
						}
						break;
					case 8:
						isRelative = FALSE;
						break;
					default:
						break;
				}
				if (!isRelative)
				{
					if (!GetFullPathNameW(lpTargetFileName, sizeof(namebuf) / sizeof(namebuf[0]), namebuf, NULL))
					{
						err = Win32GetLVFileErr();
						CloseHandle(hFile);
						return err;
					}
				}
				else
				{
					size_t rem = wcslen(lpTargetFileName);
					LPCWSTR src = lpTargetFileName;
					LPWSTR tgt = namebuf, root = namebuf;
					while (rem)
					{
						do
						{
							if (rem >= 3 && src[0] == '.' && src[1] == '.' && src[2] == kPathSeperator)
							{
								if (tgt > root)
								{
									while (tgt > root && *--tgt != kPathSeperator);
									tgt++;
								}
								else
								{
									memcpy(tgt, src, 3 * sizeof(WCHAR));
									tgt += 3;
									root += 3;
								}
								src += 3;
								rem -= 3;
							}
							else if (rem >= 2 && src[0] == '.' && src[1] == kPathSeperator)
							{
								src += 2;
								rem -= 2;
							}
						}
						while (rem-- && (*tgt++ = *src++) != kPathSeperator);
					}
					*tgt = 0;
				}
				buffer->ReparseTag = IO_REPARSE_TAG_SYMLINK;
				buffer->Reserved = 0;
				buffer->SymbolicLinkReparseBuffer.PrintNameOffset = 0;
				buffer->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)(wcslen(lpTargetFileName) * sizeof(WCHAR));
	            
				memcpy((char *)buffer->SymbolicLinkReparseBuffer.PathBuffer + buffer->SymbolicLinkReparseBuffer.PrintNameOffset,
		               lpTargetFileName, buffer->SymbolicLinkReparseBuffer.PrintNameLength);

				buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset = buffer->SymbolicLinkReparseBuffer.PrintNameOffset + buffer->SymbolicLinkReparseBuffer.PrintNameLength;
				buffer->SymbolicLinkReparseBuffer.SubstituteNameLength = (USHORT)(wcslen(namebuf) * sizeof(WCHAR));
	
				memcpy((char *)buffer->SymbolicLinkReparseBuffer.PathBuffer + buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset,
		                namebuf, buffer->SymbolicLinkReparseBuffer.SubstituteNameLength);

				buffer->SymbolicLinkReparseBuffer.Flags = isRelative ? SYMLINK_FLAG_RELATIVE : 0;
				buffer->ReparseDataLength = 12 + buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset + buffer->SymbolicLinkReparseBuffer.SubstituteNameLength;
				
				bytes = 8 + buffer->ReparseDataLength;
				if (!DeviceIoControl(hFile, FSCTL_SET_REPARSE_POINT, NULL, 0, buffer, bytes, &bytes, NULL))
				{
				    err = Win32GetLVFileErr();
					deletefunc(LWPathBuf(lwSymlinkFileName));
				}
				DSDisposePtr((UPtr)buffer);
			}
			CloseHandle(hFile);
		}
	}
	return err;
}
#endif

static MgErr lvFile_CreateLink(LWPathHandle src, LWPathHandle tgt, uInt32 flags)
{
    MgErr err = mgNotSupported;
	int8 type = LWPathTypeGet(src); 

	if (type != fAbsPath && type != fUNCPath)
		return mgArgErr;

#if MacOSX || Unix
	err = mgNoErr;
    if (flags & kLinkHard)
    {
        if (link(LWPathBuf(src), LWPathBuf(tgt)))
            err = UnixToLVFileErr();
    }
    else
    {
        if (symlink(LWPathBuf(src), LWPathBuf(tgt)))
            err = UnixToLVFileErr();
    }
#elif Win32 && !Pharlap
    // Need to acquire backup privileges in order to be able to call symlink kernel entry points
	Win32ModifyBackupPrivilege(TRUE);
    if (flags & kLinkHard)
    {
		err = Win32CreateHardLink(src, tgt, NULL);
    }
	else 
	{
		err = Win32CreateSymbolicLink(src, tgt, NULL, flags & kLinkDir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    }
 	Win32ModifyBackupPrivilege(FALSE);
#endif
    return err;
}

LibAPI(MgErr) LVFile_CreateLink(LWPathHandle *src, LWPathHandle *tgt, uInt32 flags)
{
	MgErr err = LWPathZeroTerminate(*src, NULL);
	if (!err)
	{
		err = LWPathZeroTerminate(*tgt, NULL);
		if (!err)
			err = lvFile_CreateLink(*src, *tgt, flags);
	}
	return err;
}

LibAPI(MgErr) LVPath_CreateLink(Path path, Path target, uInt32 flags)
{
    MgErr err = mgNotSupported;
    LWPathHandle src = NULL;
    LWPathHandle tgt = NULL;

	err =  LPathToLWPath(path, &src, kDefaultPath, 0);
    if (!err)
    {
        err = LPathToLWPath(target, &tgt, kDefaultPath, 0);
        if (!err)
        {
			err = lvFile_CreateLink(src, tgt, flags);
			LWPathDispose(tgt);
		}
		LWPathDispose(src);
	}
	return err;
}

LibAPI(MgErr) LVString_CreateLink(LStrHandle path, LStrHandle target, uInt32 flags)
{
    MgErr err = mgNotSupported;
    LWPathHandle src = NULL;
    LWPathHandle tgt = NULL;

	err =  LStrToLWPath(path, CP_UTF8, &src, kDefaultPath, 0);
    if (!err)
    {
        err = LStrToLWPath(target, CP_UTF8, &tgt, kDefaultPath, 0);
        if (!err)
        {
			err = lvFile_CreateLink(src, tgt, flags);
			LWPathDispose(tgt);
		}
		LWPathDispose(src);
	}
	return err;
}

static MgErr lvFile_Status(LWPathHandle lwstr, int32 end, uInt16 *fileType, uInt16 *fileRights)
{
	MgErr err = mgNoErr;
	LWChar ch = 0;
	if (end > 0)
	{
		ch = LWPathBuf(lwstr)[end];
		LWPathBuf(lwstr)[end] = 0;
	}
	{
#if Win32
		DWORD dwAttr = GetFileAttributesLW(lwstr);
		if (dwAttr == INVALID_FILE_ATTRIBUTES)
			err = Win32GetLVFileErr();
		if (!err && fileType)
			*fileType = LVFileTypeFromWinFlags(dwAttr);
		if (!err && fileRights)
			*fileRights = UnixFlagsFromWindows(Lo16(dwAttr));
#else
		struct stat statbuf;

		errno = 0;
		if (lstat(LWPathBuf(lwstr)), &statbuf) != 0)
			err = UnixToLVFileErr();
		if (!err && fileType)
			*fileType = LVFileTypeFromStat(&statbuf);
		if (!err && fileRights)
			*fileRights = Lo16(statbuf.st_mode);
#endif
	}
	if (end > 0)
	{
		LWPathBuf(lwstr)[end] = ch;
	}
	return err;
}

/* Read the path a link points to
   src: Path of the supposed link to read its destination
   tgt: Returned path with the location
   resolveDepth: kRecursive, resolve the link over multiple levels if applicable
                 kResolve, resolve the location to be an absolute path if it wasn't absolute
		         none, return relative paths if the link is relative
   resolveCount:
   fileType: the file type of the returned location
   Returns: noErr on success resolution, cancelErr if the path is not a symlink, other errors as returned by the used API functions
*/
static MgErr lvFile_ReadLink(LWPathHandle pathName, LWPathHandle *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileType)
{
	MgErr err = mgNotSupported;
	LWPathHandle src = pathName, tmp = NULL;
	int8 type = LWPathTypeGet(src); 
	int32 offset; 

	if (type != fAbsPath && type != fUNCPath)
		return mgArgErr;

	{
#if Win32 && !Pharlap
		DWORD dwAttrs = GetFileAttributesLW(src);
		if (dwAttrs == INVALID_FILE_ATTRIBUTES)
			return Win32GetLVFileErr();

		if (fileType)
			*fileType = LVFileTypeFromWinFlags(dwAttrs);

		err = mgNoErr;
		do
		{
			if (dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT)
			{
				err = Win32ResolveSymLink(src, target, resolveDepth, resolveCount, &dwAttrs);
			}
			else if (!(dwAttrs & FILE_ATTRIBUTE_DIRECTORY))
			{
				err = Win32ResolveShortCut(src, target, resolveDepth, resolveCount, &dwAttrs);
				if (!err && fileType)
					*fileType |= kIsLink;
			}
			else
				break;

			if (!err && resolveDepth < 0 || (resolveCount > 0 && resolveDepth > *resolveCount))
			{
				offset = 0;
				/* Is the link target a relative path? We allow an empty path to be equivalent to . */
				if (!LWPathLenGet(*target) || !LWPathIsOfType(*target, -1, fAbsPath))
				{
					offset = LWPathParent(src, -1);
					if (LWPathLenGet(*target) && LWPathBuf(*target)[0] != kPathSeperator)
						offset++;
				}
				if (offset && src == pathName)
					err = LWPathNCat(&tmp, 0, LWPathBuf(src), offset);
				if (LWPathLenGet(*target))
					err = LWPathNCat(&tmp, offset, LWPathBuf(*target), LWPathLenGet(*target));

				/* GetFileAttributes could fail as the symlink path does not have to point to a valid file or directory */
				if (!err)
				{
					dwAttrs = GetFileAttributesLW(tmp);
					if (dwAttrs == INVALID_FILE_ATTRIBUTES)
						break;
				
					src = tmp;
				}
			}
			else
				break;
		}
		while (!err);

		if (!err && tmp && fileType)
			*fileType |= LVFileTypeFromWinFlags(dwAttrs) << 16;

		if (err == cancelError)
			err = mgNoErr;
#elif Unix || MacOSX
		struct stat statbuf;

	    if (lstat(LWPathBuf(src), &statbuf))
		    return UnixToLVFileErr();

		*fileType = LVFileTypeFromStat(&statbuf);

		if (!S_ISLNK(statbuf.st_mode))
			/* no symlink, abort */
			return cancelError;
        
		err = mgNoErr;

		do
		{
			do
			{
				offset = statbuf.st_size;
				err = LWPathResize(target, offset);
			    if (!err)
				{
					statbuf.st_size = readlink(LWPathBuf(src), LWPathBuf(*target), offset);
					if (statbuf.st_size < 0)
						err = UnixToLVFileErr();
				}
			}
			while (!err && statbuf.st_size >= offset);

			if (err || !resolveCount || resolveDepth <= *resolveCount)
				break;

			offset = 0;
			/* Is the link target a relative path? */
			if (!statbuf.st_size || LWPathBuf(*target)[0] != kPosixPathSeperator)
			{
				offset = LWPathParent(src, -1);
				if (LWPathLenGet(*target) && LWPathBuf(*target)[0] != kPosixPathSeperator)
					offset++;
			}
			if (offset && src == *path)
				err = LWPathNCat(&tmp, 0, LWPathBuf(src), offset);
			if (statbuf.st_size)
				err = LWPathNCat(&tmp, offset, LWPathBuf(*target), statbuf.st_size);

			/* lstat could fail as the symlink path does not have to point to a valid file or directory */
			if (!err && lstat(LWPathBuf(tmp), &statbuf))
				break;

			src = tmp;
		}
		while (!err && S_ISLNK(statbuf.st_mode));

		if (!err && tmp)
			*fileType |= LVFileTypeFromStat(&statbuf) << 16;
#endif
	}
	LWPathDispose(tmp);
	return err;
}

LibAPI(MgErr) LVFile_ReadLink(LWPathHandle *pathName, LWPathHandle *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileType)
{
	MgErr err = LWPathZeroTerminate(*pathName, NULL);
	if (!err)
		err = lvFile_ReadLink(*pathName, target, resolveDepth, resolveCount, fileType);
	return err;
}

LibAPI(MgErr) LVString_ReadLink(LStrHandle path, LStrHandle *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileType)
{
    MgErr err = mgNotSupported;
#if ((Win32 && !Pharlap) || (Unix && !VxWorks) || MacOSX)
    LWPathHandle src = NULL;

	if (fileType)
		*fileType = 0;

    err = LStrToLWPath(path, CP_UTF8, &src, kDefaultPath, 0);
    if (!err)
    {
		LWPathHandle wTgt = NULL;
		err = lvFile_ReadLink(src, &wTgt, resolveDepth, resolveCount, fileType);
		if (!err && wTgt)
		{
#if usesWinPath
			int32 offset = HasDOSDevicePrefix(LWPathBuf(wTgt), LWPathLenGet(wTgt));
			if (offset == 8)
				offset = 6;
			err = WideCStrToMultiByte(LWPathBuf(wTgt) + offset, LWPathLenGet(wTgt) - offset, target, CP_UTF8, 0, NULL);
			if (!err && offset == 6)
				LStrBuf(**target)[0] = kPathSeperator;
#else
			err = ConvertFromPosixPath(LWPathBuf(wTgt), LWPathLenGet(wTgt), CP_ACP, target, CP_UTF8, '?', NULL, !(*fileType & kIsFile));
#endif
			LWPathDispose(wTgt);
		}
	    LWPathDispose(src);
	}
#endif
    return err;
}

LibAPI(MgErr) LVPath_ReadLink(Path path, Path *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileType)
{
    MgErr err = mgNotSupported;
#if ((Win32 && !Pharlap) || (Unix && !VxWorks) || MacOSX)
    LWPathHandle src = NULL;

	if (fileType)
		*fileType = 0;

    err = LPathToLWPath(path, &src, LV_TRUE, 0);
    if (!err)
    {
		LWPathHandle wTgt = NULL;
		err = lvFile_ReadLink(src, &wTgt, resolveDepth, resolveCount, fileType);
		if (!err && wTgt)
		{
			LStrHandle handle = NULL;
#if usesWinPath
			int32 offset = HasDOSDevicePrefix(LWPathBuf(wTgt), LWPathLenGet(wTgt));
			if (offset == 8)
				offset = 6;
			err = WideCStrToMultiByte(LWPathBuf(wTgt) + offset, LWPathLenGet(wTgt) - offset, &handle, CP_ACP, 0, NULL);
			if (!err && offset == 6)
				LStrBuf(*handle)[0] = kPathSeperator;
#else
			err = ConvertFromPosixPath(LWPathBuf(wTgt), LWPathLenGet(wTgt), CP_ACP, &handle, CP_ACP, '?', NULL, !(*fileType & kIsFile));
#endif
			if (!err)
				err = FTextToPath(LStrBuf(*handle), LStrLen(*handle), target);
			DSDisposeHandle((UHandle)handle);
	        LWPathDispose(wTgt);
		}
		LWPathDispose(src);
   }
#endif
    return err;
}

static MgErr lvFile_Delete(LWPathHandle pathName, LVBoolean ignoreReadOnly)
{
    MgErr err;
	int8 type = LWPathTypeGet(pathName); 
	uInt16 fileRights = 0, fileFlags = 0;

	if (type != fAbsPath && type != fUNCPath)
		return mgArgErr;	

	err = lvFile_Status(pathName, -1, &fileFlags, &fileRights);
	if (!err)
	{
		if (ignoreReadOnly && !(fileRights & 0222))
		{
#if Win32
			DWORD attr = GetFileAttributesLW(pathName);
			if (attr != INVALID_FILE_ATTRIBUTES)
			{
				if (!SetFileAttributesLW(pathName, attr &0xFFFFFFFE))
					err = Win32GetLVFileErr();
			}
		}
		if (!err)
		{
			if (fileFlags & kIsFile)
			{
				if (!DeleteFileLW(pathName))
					err = Win32GetLVFileErr();
			}
			else
			{
				if (!RemoveDirectoryLW(pathName))
					err = Win32GetLVFileErr();
			}
#else
			if (chmod(LWPathBuf(pathName), fileRights | 0222) && errno != ENOTSUP)
				err = UnixToLVFileErr();
		}
		if (!err)
		{
			if (fileFlags & kIsFile)
			{
				File fd  = (File)fopen(LWPathBuf(pathName), "a+");  /* checks for write access to file */
				if (fd)
				{
					fclose((FILE *)fd);
					if (unlink(LWPathBuf(pathName)) != 0)	       /* checks for write access to parent of file */
						err = UnixToLVFileErr();
				}
				else
					err = fNoPerm;
			}
			else
			{
				if (rmdir(LWPathBuf(pathName)) != 0)		           /* checks for write access to parent of directory */
					err = UnixToLVFileErr();
			}
#endif
		}
	}
	return err;
}

LibAPI(MgErr) LVFile_Delete(LWPathHandle *pathName, LVBoolean ignoreReadOnly)
{
	MgErr err = LWPathZeroTerminate(*pathName, NULL);
	if (!err)
		err = lvFile_Delete(*pathName, ignoreReadOnly);
	return err;
}

LibAPI(MgErr) LVString_Delete(LStrHandle pathName, LVBoolean ignoreReadOnly)
{
    LWPathHandle lwstr = NULL;
    MgErr err = LStrToLWPath(pathName, CP_UTF8, &lwstr, kDefaultPath, 0);
    if (!err)
    {
		err = LVFile_Delete(&lwstr, ignoreReadOnly);
		LWPathDispose(lwstr);
	}
	return err;
}

LibAPI(MgErr) LVPath_Delete(Path pathName, LVBoolean ignoreReadOnly)
{
    LWPathHandle lwstr = NULL;
	MgErr err = LPathToLWPath(pathName, &lwstr, kDefaultPath, 0);
    if (!err)
    {
		err = LVFile_Delete(&lwstr, ignoreReadOnly);
		LWPathDispose(lwstr);
	}
	return err;
}

static MgErr lvFile_Copy(LWPathHandle pathFrom, LWPathHandle pathTo, uInt32 replaceMode)
{
	MgErr err = mgArgErr;
	int8 typeFrom = LWPathTypeGet(pathFrom); 
	int8 typeTo = LWPathTypeGet(pathTo); 

	if ((typeFrom == fAbsPath || typeFrom == fUNCPath) && (typeTo == fAbsPath || typeTo == fUNCPath))
	{	
#if Win32

#else

#endif
	}
	return err;
}

LibAPI(MgErr) LVFile_Copy(LWPathHandle *pathFrom, LWPathHandle *pathTo, uInt32 replaceMode)
{
	MgErr err = LWPathZeroTerminate(*pathFrom, NULL);
	if (!err)
	{
		err = LWPathZeroTerminate(*pathTo, NULL);
		if (!err)
			err = lvFile_Copy(*pathFrom, *pathTo, replaceMode);
	}
	return err;
}

LibAPI(MgErr) LVString_Copy(LStrHandle pathFrom, LStrHandle pathTo, uInt32 replaceMode)
{
    LWPathHandle lwstrFrom = NULL, lwstrTo = NULL;
    MgErr err = LStrToLWPath(pathFrom, CP_UTF8, &lwstrFrom, kDefaultPath, 0);
	if (!err)
	{
		err = LStrToLWPath(pathTo, CP_UTF8, &lwstrTo, kDefaultPath, 0);
		if (!err)
		{
			err = lvFile_Copy(lwstrFrom, lwstrTo, replaceMode);
			LWPathDispose(lwstrTo);
		}
		LWPathDispose(lwstrFrom);
	}
	return err;
}

LibAPI(MgErr) LVPath_Copy(Path pathFrom, Path pathTo, uInt32 replaceMode)
{
    LWPathHandle lwstrFrom = NULL, lwstrTo = NULL;
    MgErr err = LPathToLWPath(pathFrom, &lwstrFrom, kDefaultPath, 0);
	if (!err)
	{
		err = LPathToLWPath(pathTo, &lwstrTo, LV_TRUE, 0);
		if (!err)
		{
			err = lvFile_Copy(lwstrFrom, lwstrTo, replaceMode);
			LWPathDispose(lwstrTo);
		}
		LWPathDispose(lwstrFrom);
	}
	return err;
}

static MgErr lvFile_Rename(LWPathHandle pathFrom, LWPathHandle pathTo, LVBoolean ignoreReadOnly)
{
	MgErr err = mgArgErr;
	int8 typeFrom = LWPathTypeGet(pathFrom); 
	int8 typeTo = LWPathTypeGet(pathTo); 

	if ((typeFrom == fAbsPath || typeFrom == fUNCPath) && (typeTo == fAbsPath || typeTo == fUNCPath))
	{	
		uInt16 fileRights = 0, fileType = 0;
		err = lvFile_Status(pathFrom, -1, &fileType, &fileRights);
		if (!err)
		{
			if (ignoreReadOnly && !(fileRights & 0222))
			{
#if Win32
				DWORD attr = GetFileAttributesLW(pathFrom);
				if (attr != INVALID_FILE_ATTRIBUTES)
				{
					if (!SetFileAttributesLW(pathFrom, attr &0xFFFFFFFE))
						err = Win32GetLVFileErr();
				}
			}
			if (!err)
			{
				if (!MoveFileLW(pathFrom, pathTo))
				{
					/* rename failed, try a real copy */
					err = lvFile_Copy(pathFrom, pathTo, 0);
					if (!err)
						err = lvFile_Delete(pathFrom, ignoreReadOnly);
				}
#else
				if (chmod(LWPathBuf(pathFrom), fileRights | 0222) && errno != ENOTSUP)
					err = UnixToLVFileErr();
			}
			if (!err)
			{
				struct stat statbuf;
				if (!lstat(LWPathBuf(pathTo), &statbuf))
				{
					/* If it is a link then resolve it and use the result as real target */
					if (S_ISLNK(statbuf.st_mode))
					{
						LWPathHandle lwStrLink = NULL;
						err = lvFile_ReadLink(pathTo, &lwStrLink, -1, NULL, NULL);
						if (!err)
						{
							int32 rootLen = LWPathRootLen(lwStrLink, -1, NULL);
							if (rootLen >= 0)
							{
								LWPathDispose(pathTo);
								pathTo = lwStrLink;
							}
							else
							{
								int32 len = LWPathParent(pathTo, -1);
								err = LWPathAppend(pathTo, len, NULL, lwStrLink);  
								LWPathDispose(lwStrLink);
							}
						}
					}
				}
				else
				{
					err = UnixToLVFileErr();
				}
			}
#if !VxWorks
			if (!err && !access(LWPathBuf(pathTo), F_OK))
			{
				err = fDupPath;
			}
#else
			if (!err)
			{
				int fd = open(LWPathBuf(pathTo), O_RDONLY, 0);
				if (fd < 0)
					err = fDupPath;
				else
					close(fd);
			}
#endif
			if (!err)
			{
				if (rename(LWPathBuf(pathFrom), LWPathBuf(pathTo)))
				{
					/* rename failed, try a real copy */
					err = lvFile_Copy(pathFrom, pathTo, 0);
					if (!err)
						err = lvFile_Delete(pathFrom, ignoreReadOnly);
				}
				if (!err)
				{
					if (chmod(LWPathBuf(pathTo), fileRights));
						err = UnixToLVFileErr();
				}
#endif	
			}
		}
	}
	return err;
}

LibAPI(MgErr) LVFile_Rename(LWPathHandle *pathFrom, LWPathHandle *pathTo, LVBoolean ignoreReadOnly)
{
	MgErr err = LWPathZeroTerminate(*pathFrom, NULL);
	if (!err)
	{
		err = LWPathZeroTerminate(*pathTo, NULL);
		if (!err)
			err = lvFile_Rename(*pathFrom, *pathTo, ignoreReadOnly);
	}
	return err;
}

LibAPI(MgErr) LVString_Rename(LStrHandle pathFrom, LStrHandle pathTo, LVBoolean ignoreReadOnly)
{
    LWPathHandle lwstrFrom = NULL, lwstrTo = NULL;
    MgErr err = LStrToLWPath(pathFrom, CP_UTF8, &lwstrFrom, kDefaultPath, 0);
	if (!err)
	{
		err = LStrToLWPath(pathTo, CP_UTF8, &lwstrTo, kDefaultPath, 0);
		if (!err)
		{
			err = lvFile_Rename(lwstrFrom, lwstrTo, ignoreReadOnly);
			LWPathDispose(lwstrTo);
		}
		LWPathDispose(lwstrFrom);
	}
	return err;
}

LibAPI(MgErr) LVPath_Rename(Path pathFrom, Path pathTo, LVBoolean ignoreReadOnly)
{
    LWPathHandle lwstrFrom = NULL, lwstrTo = NULL;
    MgErr err = LPathToLWPath(pathFrom, &lwstrFrom, kDefaultPath, 0);
	if (!err)
	{
		err = LPathToLWPath(pathTo, &lwstrTo, kDefaultPath, 0);
		if (!err)
		{
			err = lvFile_Rename(lwstrFrom, lwstrTo, ignoreReadOnly);
			LWPathDispose(lwstrTo);
		}
		LWPathDispose(lwstrFrom);
	}
	return err;
}

static MgErr lvFile_MoveToTrash(LWPathHandle pathName)
{
    MgErr err = mgArgErr;
	int8 type = LWPathTypeGet(pathName); 

	if (type == fAbsPath || type == fUNCPath)
	{
#if (Win32 && !Pharlap)
		IFileOperation *pFO = NULL;
		IShellItem *pSI = NULL;
		HRESULT hr = CoCreateInstance(&CLSID_FileOperation, NULL, CLSCTX_ALL, &IID_IFileOperation, &pFO);
		if (SUCCEEDED(hr))
		{
			if (IsWindows8OrGreater())
			{
				hr = IFileOperation_SetOperationFlags(pFO, FOFX_ADDUNDORECORD | FOFX_RECYCLEONDELETE | FOF_SILENT | FOF_NOERRORUI | FOFX_EARLYFAILURE);
			}
			else
			{
				hr = IFileOperation_SetOperationFlags(pFO, FOF_ALLOWUNDO | FOF_SILENT | FOF_NOERRORUI | FOFX_EARLYFAILURE);
			}
			if (SUCCEEDED(hr))
			{
				hr = SHCreateItemFromParsingName(LWPathBuf(pathName), NULL, &IID_IShellItem, &pSI);
				if (SUCCEEDED(hr))
				{
					hr = IFileOperation_DeleteItem(pFO, pSI, NULL);
					IShellItem_Release(pSI);
					if (SUCCEEDED(hr))
					{
						hr = IFileOperation_PerformOperations(pFO);
					}
				}
			}
			IFileOperation_Release(pFO);
		}
#if DEBUG
		err = SHErrToLVFileErr(hr);
#else
		err = SUCCEEDED(hr) ? mgNoErr : fIOErr;
#endif
#elif Pharlap
		// No trashcan operation, simply delete the item
		err = lvFile_Delete(pathName, LV_TRUE);
#elif  MacOSX
		err = fIOErr;

		Class NSAutoreleasePoolClass = objc_getClass("NSAutoreleasePool");
		SEL allocSel = sel_registerName("alloc");
		SEL initSel = sel_registerName("init");
		id poolAlloc = ((id(*)(Class, SEL))objc_msgSend)(NSAutoreleasePoolClass, allocSel);
		id pool = ((id(*)(id, SEL))objc_msgSend)(poolAlloc, initSel);

		Class NSStringClass = objc_getClass("NSString");
		SEL stringWithUTF8StringSel = sel_registerName("stringWithUTF8String:");
		id pathString = ((id(*)(Class, SEL, const char*))objc_msgSend)(NSStringClass, stringWithUTF8StringSel, LWPathBuf(pathName));

		Class NSFileManagerClass = objc_getClass("NSFileManager");
		SEL defaultManagerSel = sel_registerName("defaultManager");
		id fileManager = ((id(*)(Class, SEL))objc_msgSend)(NSFileManagerClass, defaultManagerSel);

		Class NSURLClass = objc_getClass("NSURL");
		SEL fileURLWithPathSel = sel_registerName("fileURLWithPath:");
		id nsurl = ((id(*)(Class, SEL, id))objc_msgSend)(NSURLClass, fileURLWithPathSel, pathString);

		SEL trashItemAtURLSel = sel_registerName("trashItemAtURL:resultingItemURL:error:");
		BOOL deleteSuccessful = ((BOOL(*)(id, SEL, id, id, id))objc_msgSend)(fileManager, trashItemAtURLSel, nsurl, nil, nil);

		if (deleteSuccessful)
		{
			err = mgNoErr;
		}

		SEL drainSel = sel_registerName("drain");
		((void(*)(id, SEL))objc_msgSend)(pool, drainSel);
#elif Unix

#else
		err = mgNotSupported;
#endif
	}
	return err;
}

LibAPI(MgErr) LVFile_MoveToTrash(LWPathHandle *pathName)
{
    MgErr err = LWPathZeroTerminate(*pathName, NULL);
	if (!err)
		err = lvFile_MoveToTrash(*pathName);
	return err;
}

LibAPI(MgErr) LVString_MoveToTrash(LStrHandle pathName)
{
	LWPathHandle lwstr = NULL;
    MgErr err = LStrToLWPath(pathName, CP_UTF8, &lwstr, kDefaultPath, 0);
    if (!err)
    {
		err = lvFile_MoveToTrash(lwstr);
		LWPathDispose(lwstr);
	}
	return err;
}

LibAPI(MgErr) LVPath_MoveToTrash(Path pathName)
{
    LWPathHandle lwstr = NULL;
	MgErr err = LPathToLWPath(pathName, &lwstr, kDefaultPath, 0);
    if (!err)
    {
		err = lvFile_MoveToTrash(lwstr);
		LWPathDispose(lwstr);
	}
	return err;
}

static MgErr lvFile_CreateDirectory(LWPathHandle lwstr, int32 end, int16 permissions)
{
	MgErr err = mgNoErr;
	LWChar ch = 0;
	if (end >= 0)
	{
		ch = LWPathBuf(lwstr)[end];
		LWPathBuf(lwstr)[end] = 0;
	}
#if Win32
	if (!CreateDirectoryLW(lwstr, NULL))
		err = Win32GetLVFileErr();
#else
#if VxWorks
	if (mkdir(LWPathBuf(lwstr)) ||
#else
	if (mkdir(LWPathBuf(lwstr), (permissions & 0777)) ||
#endif
		chmod(LWPathBuf(lwstr), (permissions & 0777))) /* ignore umask */
		err = UnixToLVFileErr();
#endif
	if (end >= 0)
	{
		LWPathBuf(lwstr)[end] = ch;
	}
	return err;
}

static MgErr lvFile_CreateDirectories(LWPathHandle pathName, int16 permissions)
{
    MgErr err = mgArgErr;
	int8 type = LWPathTypeGet(pathName); 

	if (type == fAbsPath || type == fUNCPath)
	{
		LWChar *ptr = LWPathBuf(pathName);
	 	int32 end, len = LWPathLenGet(pathName), rootLen = LWPathRootLen(pathName, HasDOSDevicePrefix(ptr, len), NULL);
		if (rootLen > 0)
		{
			uInt16 fileType = 0;
			if (len > rootLen && ptr[len - 1] == kNativePathSeperator)
				len--;
			for (end = len; end >= rootLen; end = LWPathParentInternal(pathName, rootLen, end)) 
			{
				err = lvFile_Status(pathName, end, &fileType, NULL);
				if (err != fNotFound)
					break;
			}
			while (!err && end < len)
			{
				end = LWPathNextElement(pathName, end, len);
				err = lvFile_CreateDirectory(pathName, end, permissions);
			}
		}
	}
	return err;
}

LibAPI(MgErr) LVFile_CreateDirectories(LWPathHandle *pathName, int16 permissions)
{
	MgErr err = LWPathZeroTerminate(*pathName, NULL);
	if (!err)
		err = lvFile_CreateDirectories(*pathName, permissions);
	return err;
}

LibAPI(MgErr) LVString_CreateDirectories(LStrHandle path, int16 permissions)
{
    LWPathHandle lwstr = NULL;
    MgErr err = LStrToLWPath(path, CP_UTF8, &lwstr, kDefaultPath, 0);
    if (!err)
    {
		err = lvFile_CreateDirectories(lwstr, permissions);
		LWPathDispose(lwstr);
	}
	return err;
}

LibAPI(MgErr) LVPath_CreateDirectories(Path path, int16 permissions)
{
    LWPathHandle lwstr = NULL;
    MgErr err = LPathToLWPath(path, &lwstr, kDefaultPath, 0);
    if (!err)
    {
		err = LVFile_CreateDirectories(&lwstr, permissions);
		LWPathDispose(lwstr);
	}
	return err;
}

MgErr lvfile_CloseFile(FileRefNum ioRefNum)
{
	MgErr err = mgNoErr;
#if usesWinPath
	if (!CloseHandle(ioRefNum))
	{
		err = Win32GetLVFileErr();
	}
#else
	if (fclose(ioRefNum))
	{
		err = UnixToLVFileErr();
	}
#endif
	return err;
}

static MgErr lvfile_GetSize(FileRefNum ioRefNum, FileOffset *size)
{
    MgErr err = mgNoErr;
	FileOffset tell = { 0 };

	if (0 == ioRefNum)
		return mgArgErr;
	size->q = 0;
#if usesWinPath
	tell.l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell.l.hi, FILE_CURRENT);
	if (tell.l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	size->l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&size->l.hi, FILE_END);
	if (size->l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	tell.l.lo = SetFilePointer(ioRefNum, tell.l.lo, (PLONG)&tell.l.hi, FILE_BEGIN);
	if (tell.l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value but just return with mgNoErr then
		err = Win32GetLVFileErr();
	}
#elif usesPosixPath
	errno = 0;
	tell.q = ftello64(ioRefNum);
	if (tell.q == - 1)
	{
		return UnixToLVFileErr();
	}
	else if (fseeko64(ioRefNum, 0L, SEEK_END) == -1)
	{
		return UnixToLVFileErr();
	}
	size->q = ftello64(ioRefNum);
	if (size->q == - 1)
	{
		return UnixToLVFileErr();
	}
	if (fseeko64(ioRefNum, tell.q, SEEK_SET) == -1)
	{
		return UnixToLVFileErr();
	}
#endif
	return err;
}

static MgErr lvfile_SetSize(FileRefNum ioRefNum, FileOffset *size)
{
	MgErr err = mgNoErr;
	FileOffset tell = { 0 };

	if (0 == ioRefNum)
		return mgArgErr;
	if (size->q < 0)
		return mgArgErr;
#if usesWinPath
	tell.l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell.l.hi, FILE_CURRENT);
	if (tell.l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	size->l.lo = SetFilePointer(ioRefNum, size->l.lo, (PLONG)&size->l.hi, FILE_BEGIN);
	if (size->l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	if (SetEndOfFile(ioRefNum))
	{
		if (tell.q < size->q)
		{
			tell.l.lo = SetFilePointer(ioRefNum, tell.l.lo, (PLONG)&tell.l.hi, FILE_BEGIN);
			if (tell.l.lo == INVALID_SET_FILE_POINTER)
			{
				// INVALID_FILE_SIZE could be a valid value but just return anyways with mgNoErr
				err = Win32GetLVFileErr();
			}
		}
	}
	else
	{
		err = Win32GetLVFileErr();
	}
#else
	errno = 0;
	if (fflush(ioRefNum) != 0)
	{
		return fIOErr;
	}
	if (ftruncate64(fileno(ioRefNum), size->q) != 0)
	{
		return UnixToLVFileErr();
	}
	tell.q = ftello64(ioRefNum);
	if (tell.q == -1)
	{
		return UnixToLVFileErr();
	}
	if ((tell.q > size->q) && (fseeko64(ioRefNum, size->q, SEEK_SET) != 0))
	{
		return UnixToLVFileErr();
	}
#endif
	return err;
}

static MgErr lvfile_SetFilePos(FileRefNum ioRefNum, FileOffset *offs, uInt16 mode)
{
	MgErr err = mgNoErr;
	FileOffset size, sought, tell;

	if (0 == ioRefNum)
		return mgArgErr;

	if ((offs->q == 0) && (mode == fCurrent))
		return noErr;
#if usesWinPath
	size.l.lo = GetFileSize(ioRefNum, (LPDWORD)&size.l.hi);
	if (size.l.lo == INVALID_FILE_SIZE)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}

	if (mode == fStart)
	{
		sought.q = offs->q;
	}
	else if (mode == fCurrent)
	{
		tell.l.hi = 0;
		tell.l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell.l.hi, FILE_CURRENT);
		if (tell.l.lo == INVALID_SET_FILE_POINTER)
		{	
		    // INVALID_FILE_SIZE could be a valid value
			err = Win32GetLVFileErr();
			if (err)
				return err;
		}
		sought.q = tell.q + offs->q;
	}
	else /* fEnd */
	{
		sought.q = size.q + offs->q;
	}

	if (sought.q > size.q)
	{
		SetFilePointer(ioRefNum, 0L, 0L, FILE_END);
		err = fEOF;
	}
	else if (sought.q < 0)
	{
		SetFilePointer(ioRefNum, 0L, 0L, FILE_BEGIN);
		err = fEOF;
	}
	else
	{
		sought.l.lo = SetFilePointer(ioRefNum, sought.l.lo, (PLONG)&sought.l.hi, FILE_BEGIN);
		if (sought.l.lo == INVALID_SET_FILE_POINTER)
		{
			// INVALID_FILE_SIZE could be a valid value but just return anyways with mgNoErr
			err = Win32GetLVFileErr();
		}
	}
#else
	errno = 0;
	if (mode == fCurrent)
	{
		tell.q = ftello64(ioRefNum);
		if (tell.q == -1)
		{
			return UnixToLVFileErr();
		}
		sought.q = tell.q + offs->q;
	}
	if (fseeko64(ioRefNum, 0L, SEEK_END) != 0)
	{
		return UnixToLVFileErr();
	}
	size.q = ftello64(ioRefNum);
	if (size.q == -1)
	{
		return UnixToLVFileErr();
	}
	if (mode == fStart)
	{
		sought.q = offs->q;
	}
	else /* fEnd */
	{
		sought.q = size.q + offs->q;
	}

	if (sought.q > size.q)
	{
		/* already moved to actual end of file above */
		return fEOF;
	}
	else if (sought.q < 0)
	{
		fseeko64(ioRefNum, 0L, SEEK_SET);
		return fEOF;
	}
	else if (fseeko64(ioRefNum, sought.q, SEEK_SET) != 0)
	{
		return UnixToLVFileErr();
	}
#endif
	return err;
}

static MgErr lvfile_GetFilePos(FileRefNum ioRefNum, FileOffset *tell)
{
	if (0 == ioRefNum)
		return mgArgErr;
#if usesWinPath
	tell->l.hi = 0;
	tell->l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell->l.hi, FILE_CURRENT);
	if (tell->l.lo == INVALID_SET_FILE_POINTER)
	{
		return Win32GetLVFileErr();
	}
#else
	errno = 0;
	tell->q = ftello64(ioRefNum);
	if (tell->q == -1)
	{
		return UnixToLVFileErr();
	}
#endif
	return mgNoErr;
}

static MgErr lvfile_Read(FileRefNum ioRefNum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
{
    MgErr	err = mgNoErr;
#if usesWinPath
	DWORD actCount;
#else
	int actCount;
#endif

    if (0 == ioRefNum)
		return mgArgErr;
    if (outCount)
        *outCount = 0;

#if usesWinPath
	if (!ReadFile(ioRefNum, buffer, inCount, &actCount, NULL))
	{
		return Win32GetLVFileErr();
	}
	if (actCount != inCount)
	{
		err = fEOF;
	}
#else
	errno = 0;
	actCount = fread((char *)buffer, 1, inCount, ioRefNum);
	if (ferror(ioRefNum))
	{
		clearerr(ioRefNum);
        return fIOErr;
	}
	if (feof(ioRefNum))
	{
		clearerr(ioRefNum);
        err = fEOF;
	}
#endif
    if (outCount)
        *outCount = actCount;
    return err;
}

static MgErr lvfile_Write(FileRefNum ioRefNum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
{
    MgErr err = mgNoErr;
#if usesWinPath
	DWORD actCount;
#else
	int actCount;
#endif

	if (0 == ioRefNum)
		return mgArgErr;
    if (outCount)
        *outCount = 0;

#if usesWinPath
	if (!WriteFile(ioRefNum, buffer, inCount, &actCount, NULL))
    {
		return Win32GetLVFileErr();
    }
#else
    errno = 0;
    actCount = fwrite((char *)buffer, 1, inCount, ioRefNum);
    if (ferror(ioRefNum))
    {
        clearerr(ioRefNum);
        return fIOErr;
    }
#endif
    if (outCount)
        *outCount = actCount;
    return err;
}

#if MacOSX
static char *namedResourceFork = "/..namedfork/rsrc";
#endif

static MgErr lvFile_OpenFile(FileRefNum *ioRefNum, LWPathHandle lwstr, uInt32 rsrc, uInt32 createMode, uInt32 openMode, uInt32 denyMode)
{
    MgErr err = noErr;
	uInt8 type = LWPathTypeGet(lwstr);
	int32 len = LWPathLenGet(lwstr);
#if usesPosixPath
    char theMode[4];
#elif usesWinPath
    DWORD shareAcc, openAcc;
    DWORD createAcc = OPEN_EXISTING;
    int32 attempts = 3;
 #if !Pharlap
	wchar_t *rsrcPostfix = NULL;
 #endif
#endif

	if (!len || (type != fAbsPath && type != fUNCPath))
		return mgArgErr;

#if usesWinPath
 #if Pharlap
	if (rsrc)
		return mgArgErr;
 #else
	/* Try to access the possible NTFS alternate streams from Services For Macintosh (SFM) */
	switch (rsrc)
	{
		case kOpenFileRsrcData:
			break;
		case kOpenFileRsrcResource:
			rsrcPostfix = L":AFP_Resource";
			break;
		case kOpenFileRsrcInfo:
			rsrcPostfix = L":AFP_AfpInfo";
			break;
		case kOpenFileRsrcDesktop:
			rsrcPostfix = L":AFP_DeskTop";
			break;
		case kOpenFileRsrcIndex:
			rsrcPostfix = L":AFP_IdIndex";
			break;
		case kOpenFileRsrcComment:
			rsrcPostfix = L":Comments";
			break;
		default:
			return mgArgErr;
	}
 #endif

    switch (openMode)
    {
		case openReadOnly:
			openAcc = GENERIC_READ;
			break;
		case openWriteOnlyTruncate:
			createAcc = TRUNCATE_EXISTING;
			/* Intentionally falling through */
		case openWriteOnly:
			openAcc = GENERIC_WRITE;
			break;
		case openReadWrite:
			openAcc = GENERIC_READ | GENERIC_WRITE;
			break;
		default:
			return mgArgErr;
    }

    switch (denyMode)
    {
		case denyReadWrite:
			shareAcc = 0;
			break;
		case denyWriteOnly:
			shareAcc = FILE_SHARE_READ;
			break;
		case denyNeither:
			shareAcc = FILE_SHARE_READ | FILE_SHARE_WRITE ;
			break;
		default:
			return mgArgErr;
    }

    switch (createMode)
    {
		case createNormal:
			createAcc = CREATE_NEW;
			break;
		case createAlways:
			createAcc = CREATE_ALWAYS;
			break;
	 }

 #if !Pharlap
	if (rsrcPostfix)
	{
		err = LWPathNCat(&lwstr, -1, rsrcPostfix, -1);
	}
#endif
	/* Open the specified file. */
    while (!err)
	{
		*ioRefNum = CreateFileLW(lwstr, openAcc, shareAcc, 0, createAcc, FILE_ATTRIBUTE_NORMAL, 0);
		if (*ioRefNum == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error == ERROR_SHARING_VIOLATION && (--attempts > 0))
		    {
			    Sleep(50);
		    }
		    else
			{
				err = Win32ToLVFileErr(error);
			}
		}
		else
		{
			break;
		}
	}
	if (err)
		return err;

#if !Pharlap
	if (rsrcPostfix)
	{
		LWPathLenSet(lwstr, len);
	}
#endif
#else
 #if MacOSX
	if (rsrc > kOpenFileRsrcResource)
 #else
	if (rsrc)
 #endif
		return mgArgErr;

	if (!createMode)
	{
		switch (openMode)
		{
			case openWriteOnly:
				/* Treat write-only as read-write, since we can't open a file for write-only
				   using buffered i/o functions without truncating the file. */
			case openReadWrite:
				strcpy(theMode, "r+");
				break;
			case openReadOnly:
				strcpy(theMode, "r");
				break;
			case openWriteOnlyTruncate:
				strcpy(theMode, "w");
				break;
			default:
				return mgArgErr;
		}
	}
	else if (createMode == createNormal)
	{
		strcpy(theMode, "r");
	}
	else
	{
		strcpy(theMode, "w+");
	}

	switch (denyMode)
	{
		case denyReadWrite:
		case denyWriteOnly:
		case denyNeither:
			break;
		default:
			return mgArgErr;
	}

 #if MacOSX
	if (rsrc == kOpenFileRsrcResource)
	{
		err = LWPathNCat(lwstr, -1, namedResourceFork, -1);
		if (err)
			return err;
	}
 #endif

	/* Test for file existence first to avoid creating file with mode "w". */
	if (openMode == openWriteOnlyTruncate)
	{
		uInt16 fileType;
		err = lvFile_Status(lwstr, -1, &fileType, NULL);
		if (err || fileType != kIsFile)
			return fNotFound;
	}

	errno = 0;
	*ioRefNum = fopen(LWPathBuf(lwstr), (char *)theMode);
	if (!*ioRefNum)
		return UnixToLVFileErr();

 #ifdef HAVE_FCNTL
	/* Implement deny mode by range locking whole file */
	if (denyMode == denyReadWrite || denyMode == denyWriteOnly)
	{
		struct flock lockInfo;

		lockInfo.l_type = (openMode == openReadOnly) ? F_RDLCK : F_WRLCK;
		lockInfo.l_whence = SEEK_SET;
		lockInfo.l_start = 0;
		lockInfo.l_len = 0;
		if (fcntl(fileno(*ioRefNum), F_SETLK, FCNTL_PARAM3_CAST(&lockInfo)) == -1)
		{
			err = UnixToLVFileErr();
		}
	}
 #endif
#endif
	if (err)
	{
		lvfile_CloseFile(*ioRefNum);
		DEBUGPRINTF(((CStr)"OpenFile: err = %ld, rsrc = %d", err, (int16)rsrc));
	}
	return err;
}

LibAPI(MgErr) LVFile_CreateFile(LVRefNum *refnum, LWPathHandle *pathName, uInt32 rsrc, uInt32 openMode, uInt32 denyMode, LVBoolean always)
{
	MgErr err = LWPathZeroTerminate(*pathName, NULL);
	if (!err)
	{
		FileRefNum ioRefNum = NULL;
		err = lvFile_OpenFile(&ioRefNum, *pathName, rsrc, always ? createAlways : createNormal, openMode, denyMode);
		if (!err)
		{
			err = lvzlibCreateRefnum(ioRefNum, refnum, FileMagic, LV_TRUE);
		}
	}
	return err;
}

LibAPI(MgErr) LVString_CreateFile(LVRefNum *refnum, LStrHandle pathName, uInt32 rsrc, uInt32 openMode, uInt32 denyMode, LVBoolean always)
{
#if (usesWinPath && !Pharlap)
	int32 bufLen = rsrc ? 14 : 0;
#elif MacOSX
	int32 bufLen = rsrc ? strlen(namedResourceFork) : 0;
#else
	int32 bufLen = 0;
#endif
	LWPathHandle lwstr = NULL;
	MgErr err = LStrToLWPath(pathName, CP_UTF8, &lwstr, kDefaultPath, bufLen);
	if (!err)
	{
		FileRefNum ioRefNum = NULL;
		err = lvFile_OpenFile(&ioRefNum, lwstr, rsrc, always ? createAlways : createNormal, openMode, denyMode);
		LWPathDispose(lwstr);
		if (!err)
		{
			err = lvzlibCreateRefnum(ioRefNum, refnum, FileMagic, LV_TRUE);
		}
	}
	return err;
}

LibAPI(MgErr) LVPath_CreateFile(LVRefNum *refnum, Path pathName, uInt32 rsrc, uInt32 openMode, uInt32 denyMode, LVBoolean always)
{
#if (usesWinPath && !Pharlap)
	int32 bufLen = rsrc ? 14 : 0;
#elif MacOSX
	int32 bufLen = rsrc ? strlen(namedResourceFork) : 0;
#else
	int32 bufLen = 0;
#endif
	LWPathHandle lwstr = NULL;
	MgErr err = LPathToLWPath(pathName, &lwstr, kDefaultPath, bufLen);
	if (!err)
	{
		FileRefNum ioRefNum = NULL;
		err = lvFile_OpenFile(&ioRefNum, lwstr, rsrc, always ? createAlways : createNormal, openMode, denyMode);
		LWPathDispose(lwstr);
		if (!err)
		{
			err = lvzlibCreateRefnum(ioRefNum, refnum, FileMagic, LV_TRUE);
		}
	}
	return err;
}

LibAPI(MgErr) LVFile_OpenFile(LVRefNum *refnum, LWPathHandle *pathName, uInt32 rsrc, uInt32 openMode, uInt32 denyMode)
{
	MgErr err = LWPathZeroTerminate(*pathName, NULL);
	if (!err)
	{
		FileRefNum ioRefNum = NULL;
		err = lvFile_OpenFile(&ioRefNum, *pathName, rsrc, createNone, openMode, denyMode);
		if (!err)
		{
			err = lvzlibCreateRefnum(ioRefNum, refnum, FileMagic, LV_TRUE);
		}
	}
	return err;
}

LibAPI(MgErr) LVString_OpenFile(LVRefNum *refnum, LStrHandle pathName, uInt32 rsrc, uInt32 openMode, uInt32 denyMode)
{
#if (usesWinPath && !Pharlap)
	int32 bufLen = rsrc ? 14 : 0;
#elif MacOSX
	int32 bufLen = rsrc ? strlen(namedResourceFork) : 0;
#else
	int32 bufLen = 0;
#endif
	LWPathHandle lwstr = NULL;
	MgErr err = LStrToLWPath(pathName, CP_UTF8, &lwstr, kDefaultPath, bufLen);
	if (!err)
	{
		FileRefNum ioRefNum = NULL;
		err = lvFile_OpenFile(&ioRefNum, lwstr, rsrc, createNone, openMode, denyMode);
		LWPathDispose(lwstr);
		if (!err)
		{
			err = lvzlibCreateRefnum(ioRefNum, refnum, FileMagic, LV_TRUE);
		}
	}
	return err;
}

LibAPI(MgErr) LVPath_OpenFile(LVRefNum *refnum, Path pathName, uInt32 rsrc, uInt32 openMode, uInt32 denyMode)
{
#if (usesWinPath && !Pharlap)
	int32 bufLen = rsrc ? 14 : 0;
#elif MacOSX
	int32 bufLen = rsrc ? strlen(namedResourceFork) : 0;
#else
	int32 bufLen = 0;
#endif
	LWPathHandle lwstr = NULL;
	MgErr err = LPathToLWPath(pathName, &lwstr, kDefaultPath, bufLen);
	if (!err)
	{
		FileRefNum ioRefNum = NULL;
		err = lvFile_OpenFile(&ioRefNum, lwstr, rsrc, createNone, openMode, denyMode);
		LWPathDispose(lwstr);
		if (!err)
		{
			err = lvzlibCreateRefnum(ioRefNum, refnum, FileMagic, LV_TRUE);
		}
	}
	return err;
}

LibAPI(MgErr) LVFile_CloseFile(LVRefNum *refnum)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibDisposeRefnum(refnum, &ioRefNum, FileMagic);
	if (!err)
	{
		err = lvfile_CloseFile(ioRefNum);
	}
	return err;
}

LibAPI(MgErr) LVFile_GetSize(LVRefNum *refnum, FileOffset *size)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, &ioRefNum, FileMagic);
	if (!err)
	{
		err = lvfile_GetSize(ioRefNum, size);
	}
	return err;
}

LibAPI(MgErr) LVFile_SetSize(LVRefNum *refnum, FileOffset *size)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, &ioRefNum, FileMagic);
	if (!err)
	{
		err = lvfile_SetSize(ioRefNum, size);
	}
	return err;
}

LibAPI(MgErr) LVFile_SetFilePos(LVRefNum *refnum, FileOffset *offs, uInt16 mode)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, &ioRefNum, FileMagic);
	if (!err)
	{
		err = lvfile_SetFilePos(ioRefNum, offs, mode);
	}
	return err;
}

LibAPI(MgErr) LVFile_GetFilePos(LVRefNum *refnum, FileOffset *offs)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, &ioRefNum, FileMagic);
	if (!err)
	{
		err = lvfile_GetFilePos(ioRefNum, offs);
	}
	return err;
}

LibAPI(MgErr) LVFile_Read(LVRefNum *refnum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, &ioRefNum, FileMagic);
	if (!err)
	{
		err = lvfile_Read(ioRefNum, inCount, outCount, buffer);
	}
	return err;
}

LibAPI(MgErr) LVFile_Write(LVRefNum *refnum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, &ioRefNum, FileMagic);
	if (!err)
	{
		err = lvfile_Write(ioRefNum, inCount, outCount, buffer);
	}
	return err;
}

LibAPI(MgErr) InitializeFileFuncs(LStrHandle filefunc_def)
{
	MgErr err = NumericArrayResize(uB, 1, (UHandle*)&filefunc_def, sizeof(zlib_filefunc64_def));
	if (!err)
	{
		zlib_filefunc64_def* pzlib_filefunc_def = (zlib_filefunc64_def*)LStrBuf(*filefunc_def);
		LStrLen(*filefunc_def) = sizeof(zlib_filefunc64_def);
#if Win32
		fill_win32_filefunc64W(pzlib_filefunc_def);
#else
		fill_fopen64_filefunc(pzlib_filefunc_def);
#endif
	}
	return err;
}

LibAPI(MgErr) InitializeStreamFuncs(LStrHandle filefunc_def, LStrHandle *memory)
{
	MgErr err = NumericArrayResize(uB, 1, (UHandle*)&filefunc_def, sizeof(zlib_filefunc64_def));
	if (!err)
	{
		zlib_filefunc64_def* pzlib_filefunc_def = (zlib_filefunc64_def*)LStrBuf(*filefunc_def);
		LStrLen(*filefunc_def) = sizeof(zlib_filefunc64_def);

		fill_mem_filefunc(pzlib_filefunc_def, memory);
	}
	return err;
}
