/* 
   lvutil.c -- support functions for LabVIEW ZIP library

   Copyright (C) 2002-2024 Rolf Kalbermatter

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

#define _GNU_SOURCE

#include "lvutil.h"
#include "zlib.h"
#include "ioapi.h"
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
 #if !Pharlap
  #include "versionhelper.h"
 #endif

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

#ifndef S_IFMT
#define S_IFMT		0xF000	/* mask */
#endif
#ifndef S_IFCHR
#define S_IFCHR		0x2000	/* character device */
#endif
#ifndef S_IFDIR
#define S_IFDIR		0x4000	/* directory */
#endif
#ifndef S_IFBLK
#define S_IFBLK		0x6000	/* device */
#endif
#ifndef S_IFREG
#define S_IFREG		0x8000	/* regular file */
#endif
#ifndef	S_IFLNK
#define S_IFLNK		0xA000	/* syslink */
#endif

#ifndef	S_IRUSR
#define S_IRUSR		0x0100	/* read permission, owner 0400 */
#endif
#ifndef	S_IWUSR
#define S_IWUSR		0x0080	/* write permission, owner 0200 */
#endif
#ifndef	S_IXUSR
#define S_IXUSR		0x0040	/* execute/search permission, owner 0100 */
#endif
#ifndef	S_IRWXU
#define S_IRWXU		0x01C0	/* This is equivalent to �(S_IRUSR | S_IWUSR | S_IXUSR) */
#endif

#if Pharlap
 static const char strWildcardPattern[] = "*.*";
 #define COPY_FILE_FAIL_IF_EXISTS   1
 #define WIN32_FIND_DATALW						WIN32_FIND_DATAA
 #define CreateFileLW(path, fl, sh, ds, op, se, ov)	CreateFileA(LWPathBuf(path), fl, sh, ds, op, se, ov)
 #define CreateDirectoryLW(path, sec)			CreateDirectoryA(LWPathBuf(path), sec)
 #define GetFileAttributesLW(path)				GetFileAttributesA(LWPathBuf(path))
 #define SetFileAttributesLW(path, attr)		SetFileAttributesA(LWPathBuf(path), attr)
 #define FindFirstFileLW(path, findFiles)		FindFirstFileA(LWPathBuf(path), findFiles)
 #define FindNextFileLW(handle, findFiles)		FindNextFileA(handle, findFiles)
 #define RemoveDirectoryLW(path)				RemoveDirectoryA(LWPathBuf(path))
 #define DeleteFileLW(path)						DeleteFileA(LWPathBuf(path))
 #define MoveFileLW(pathFrom, pathTo)			MoveFileA(LWPathBuf(pathFrom), LWPathBuf(pathTo))
 #define CopyFileLW(pathFrom, pathTo, progress, data, cancel, flags)	CopyFileA(LWPathBuf(pathFrom), LWPathBuf(pathTo), flags & COPY_FILE_FAIL_IF_EXISTS != 0)
 #define GetLogicalDriveStringsLW(length, buf)	GetLogicalDriveStringsA(length, buf)
#else
 static const wchar_t strWildcardPattern[] = L"*.*";
 #define WIN32_FIND_DATALW						WIN32_FIND_DATAW
 #define CreateFileLW(path, fl, sh, ds, op, se, ov) CreateFileW(LWPathBuf(path), fl, sh, ds, op, se, ov)
 #define CreateDirectoryLW(path, sec)			CreateDirectoryW(LWPathBuf(path), sec)
 #define GetFileAttributesLW(path)				GetFileAttributesW(LWPathBuf(path))
 #define SetFileAttributesLW(path, attr)		SetFileAttributesW(LWPathBuf(path), attr)
 #define FindFirstFileLW(path, findFiles)		FindFirstFileExW(LWPathBuf(path), FindExInfoBasic, findFiles, FindExSearchNameMatch, NULL, 0)
 #define FindNextFileLW(handle, findFiles)		FindNextFileW(handle, findFiles)
 #define RemoveDirectoryLW(path)				RemoveDirectoryW(LWPathBuf(path))
 #define DeleteFileLW(path)						DeleteFileW(LWPathBuf(path))
 #define MoveFileLW(pathFrom, pathTo)			MoveFileW(LWPathBuf(pathFrom), LWPathBuf(pathTo))
 #define MoveFileWithProgressLW(pathFrom, pathTo, progress, data, flags) MoveFileWithProgressW(LWPathBuf(pathFrom), LWPathBuf(pathTo), progress, data, flags)
 #define CopyFileLW(pathFrom, pathTo, progress, data, cancel, flags)   CopyFileExW(LWPathBuf(pathFrom), LWPathBuf(pathTo), progress, data, cancel, flags)
 #define GetLogicalDriveStringsLW(length, buf)	GetLogicalDriveStringsW(length, buf)
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

#define REPARSE_DATA_BUFFER_HEADER_SIZE		FIELD_OFFSET(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer)
#define REPARSE_DATA_SYMBOLIC_HEADER_SIZE   FIELD_OFFSET(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer)
#define REPARSE_DATA_MOUNTPOINT_HEADER_SIZE FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer)

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
 #include <sys/time.h>
 #define ftruncate64 ftruncate
 #include <wchar.h>

 #define TRUE 1
 #define FALSE 0

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
  #define st_atimespec     st_atim	/* Time of last access */
  #define st_mtimespec     st_mtim	/* Time of last modification */
  #define st_ctimespec     st_ctim	/* Time of last status change */
 #endif
#elif MacOSX
 #include <CoreFoundation/CoreFoundation.h>
 #include <CoreServices/CoreServices.h>
 #include <objc/runtime.h>
 #include <objc/message.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <sys/time.h>
 #include <sys/attr.h>
 #include <sys/xattr.h>
 #include <dirent.h>
 #define ftruncate64 ftruncate

 #define kFinfoIsInvisible (OSSwapHostToBigConstInt16(kIsInvisible))

typedef	struct finderinfo {
    u_int32_t type;
    u_int32_t creator;
    u_int16_t finderFlags;
    Point location;
    u_int16_t reserved1;
    u_int32_t reserved2[2];
	u_int16_t extendedFlags;
	int16_t reserved3;
	int32_t putAwayFolderID
} __attribute__ ((__packed__)) finderinfo;

typedef struct fileinfobuf {
    u_int32_t info_length;
    u_int32_t data[8];
} fileinfobuf;
#endif

#include "lwstr.h"

/*
  The version 1 format of the resource file on disk is as follows:
  magic header -- 4 bytes ('RSRC')
  version number -- 4 bytes (1)
  map offset -- 4 bytes
  map length -- 4 bytes
  data offset -- 4 bytes
  data length -- 4 bytes
  ... [ the data ]
  */

/*
  The version 2 format of the resource file on disk is as follows:
  magic header -- 4 bytes ('RSRC')
  version number -- 4 bytes (2)
  file type -- 4 bytes
  file creator -- 4 bytes
  map offset -- 4 bytes
  map length -- 4 bytes
  data offset -- 4 bytes
  data length -- 4 bytes
  ... [ the data ]
  */

/*
  The version 3 format of the resource file on disk is as follows:
  magic header -- 4 bytes ('RSRC')
  corruption check -- 2 bytes
  version number -- 2 bytes (3)
  file type -- 4 bytes
  file creator -- 4 bytes
  map offset -- 4 bytes
  map length -- 4 bytes
  data offset -- 4 bytes
  data length -- 4 bytes
  ... [ the data ]
  */

/*
  The version 4 format of the resource file on disk is same as 3 but the resources are sorted
  */

#define NOT_CORRUPT					 RTToW(0xd, 0xa)
#define RSRC_MAGIC					 RTToL('R','S','R','C')

typedef uInt32 RsrcID;

#define TypeMapPtr(m) (TypeMap *)(((UPtr)(m)) + (m)->typeOffset)

#define RSRC_HEADER_FIELDS \
	ResType magic; \
	int16 isCorrupt; \
	int16 version; \
	int32 type; \
	int32 creator; \
	int32 mapOffset; \
	int32 mapLength; \
	int32 dataOffset; \
	int32 dataLength

typedef struct
{
	RSRC_HEADER_FIELDS;
} RsrcHeader, *RsrcHeaderPtr;

typedef struct
{
	ResType magic;
	int32 version;
	int32 mapOffset;
	int32 mapLength;
	int32 dataOffset;
	int32 dataLength;
} RsrcOldHeader;

typedef struct
{
	RSRC_HEADER_FIELDS;
	uInt32 unused;				/* still here to preserve size for saved files */
	FileRefNum rsrcRefnum;
	int32 rsrcFileAttr;
	int32 typeOffset;			/* Offset from beginning of resource map to type list. */
	int32 nameOffset;			/* Offset from beginning of resource map to resource name list.
								   (zero if no  names.) */
} RsrcMap, *RsrcMapPtr, * *RsrcMapHdl;

typedef struct
{
	ResType rsrcType;
	int32 numRsrcs; /* minus 1 */
	int32 listOffset;
} TypeList;

typedef struct
{
	int32 numTypes; /* minus 1 */
	TypeList type[1];
} TypeMap;

typedef struct
{
	RsrcID rsrcID;
	int32 nameOffset;			/* or -1 if no name */
	int32 rsrcAttr;
	int32 dataOffset;			/* Offset from beginning of resource */
	/* data to beginning of data for this */
	/* resource.  */
	UHandle dataHdl;
} RsrcList;

typedef struct
{
	int32 length;
	char *theName;
} NameList;

typedef struct  			/* archive directory entry */
{
	int32	id,				/* resource ID */
	        type,			/* file type */
	        vers,			/* file version */
	        flags,
	        reserved1,
	        reserved2;
	uInt32	mdate, cdate;
	uChar	name[4];		/* null padded to long boundary */
} ADEntry;
#define NextADEntry(ap)		((ADEntry*)((UPtr)ap + Rtm4(sizeof(ADEntry) - 4 + PStrLen(ap->name) + 1)))

#define kArcMagic			RTToL('a', 'd', 'i', 'r')
#define kNewADir			1L
#define kFixedMagicVers		2L
#define kCurArcVersion		2L

typedef struct
{
	int32	zero;		/* should always be zero */
	int32	magic;		/* should always be kArcMagic */
	int32	version;	/* archive mgr. private version number */
	int32	hdr1;		/* reserved for future use */
	int32	hdr2;		/* reserved for future use */
	int32	n;			/* number of entries */
} ADirHdr;

typedef struct  			/* archive directory */
{
	ADirHdr h;
	ADEntry e[1];
} ADir;

typedef struct  			/* palette menu entry */
{
	int16	nameId;
	int8	flags;
	int8	clan;
	int32	fileNum;
	uChar	objClass[2];	/* null padded to word boundary */
} PMEntry;

typedef struct  			/* palette menu */
{
	int16	ncols, nitems;
	PMEntry e[1];
} PMenu;

typedef struct
{
	int32 nameOffset;
	int32 flags;
	int32 type;
	int32 id;
	LVPoint pos;
} ITblRec;				/* Used only by ArchiveDirList and ANameCmp */
#define CompatPALM311 1
enum
{
    kEPMok = 1,
    kEPMcancel,
    kEPMfileMenu,
    kEPMcreateDate,
    kEPMmodDate,
    kEPMdelete,
    kEPMicon,
    kEPMautoLoad
};

static MgErr lvFile_OpenFile(FileRefNum *ioRefNum, LWPathHandle lwstr, uInt32 rsrc, uInt32 openMode, uInt32 accessMode, uInt32 denyMode, uInt32 flags);
static MgErr lvFile_GetSize(FileRefNum ioRefNum, LVBoolean remain, int64 *size);
static MgErr lvFile_SetSize(FileRefNum ioRefNum, int64 size);
static MgErr lvFile_GetFilePos(FileRefNum ioRefNum, int64 *offset);
static MgErr lvFile_SetFilePos(FileRefNum ioRefNum, uInt32 mode, int64 offset);
static MgErr lvFile_Read(FileRefNum ioRefNum, uInt32 inCount, uInt32 *outCount, UPtr buffer);
static MgErr lvFile_Write(FileRefNum ioRefNum, uInt32 inCount, uInt32 *outCount, UPtr buffer);

static MgErr lvFile_FileFlags(LWPathHandle lwstr, int32 end, uInt32 *fileType);
static MgErr lvFile_FileInfo(LWPathHandle pathName, uInt8 write, FileInfoPtr fileInfo);
static MgErr lvFile_HasResourceFork(LWPathHandle pathName, LVBoolean *hasResFork, FileOffset *size);
static MgErr lvFile_GetFileTypeAndCreator(LWPathHandle pathName, ResType *type, ResType *creator);
static MgErr lvFile_ListArchive(LWPathHandle pathName, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 mode);
static MgErr lvFile_ListDirectory(LWPathHandle pathName, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 mode, int32 resolveDepth);
static MgErr lvFile_CreateLink(LWPathHandle src, LWPathHandle tgt, uInt32 flags);
static MgErr lvFile_ReadLink(LWPathHandle pathName, LWPathHandle *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileType);
static MgErr lvFile_CreateDirectories(LWPathHandle src, int16 permissions);
static MgErr lvFile_CreateDirectory(LWPathHandle lwstr, int32 end, int16 permissions);
static MgErr lvFile_MoveFile(LWPathHandle pathFrom, LWPathHandle pathTo, uInt32 flags);
static MgErr lvFile_MoveRecursive(LWPathHandle pathFrom, LWPathHandle pathTo, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 flags);
static MgErr lvFile_CopyFile(LWPathHandle pathFrom, LWPathHandle pathTo, uInt32 flags);
static MgErr lvFile_CopyRecursive(LWPathHandle pathFrom, LWPathHandle pathTo, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 flags);
static MgErr lvFile_DeleteFile(LWPathHandle pathName, LVBoolean ignoreReadOnly, LVBoolean symLink);
static MgErr lvFile_DeleteDir(LWPathHandle pathName, LVBoolean ignoreReadOnly);
static MgErr lvFile_DeleteRecursive(LWPathHandle pathName, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 flags);
static MgErr lvFile_MoveToTrash(LWPathHandle pathName, uInt32 flags);

#define NameArrElms(nameArr, start) (nameArr ? (((*nameArr)->elm + start)) : NULL)
#define TypeArrElms(typeArr, start) (typeArr ? (((*typeArr)->elm + start)) : NULL)
#define NameArrSize(nameArr) (nameArr ? ((int32)((DSGetHandleSize((UHandle)nameArr) - offsetof(LStrArrRec, elm)) / sizeof(UHandle))) : 0)
#define TypeArrSize(typeArr) (typeArr ? ((int32)((DSGetHandleSize((UHandle)typeArr) - offsetof(FileTypeArrRec, elm)) / (2 * sizeof(uInt32)))) : 0)
#define TypeArrItems(typeArr) (typeArr ? (((*typeArr)->numItems)) : 0)

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

/* int64 100ns intervals from Jan 1 1601 GMT to Jan 1 1904 GMT */
#define LV1904_FILETIME_OFFSET  0x0153b281e0fb4000
#define SECS_TO_FT_MULT			10000000
#define HUNDRED_NS_TO_U64       0x1AD7F29ABCB

LibAPI(void) ATimeToFileTime(ATime128 *pt, uInt64 *pft)
{
	*pft = pt->u.f.val * SECS_TO_FT_MULT;
	*pft += pt->u.f.fract / HUNDRED_NS_TO_U64;
	*pft += LV1904_FILETIME_OFFSET;
}
  
LibAPI(void) FileTimeToATime(uInt64 *pft, ATime128 *pt)
{
	uInt64 temp = *pft;
	temp -= LV1904_FILETIME_OFFSET;
	pt->u.f.val = temp / SECS_TO_FT_MULT;
	pt->u.f.fract = (temp % SECS_TO_FT_MULT) * HUNDRED_NS_TO_U64;
}

#if Win32
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
		case ERROR_HANDLE_EOF:        return fEOF;
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
		case DE_FILEDESTISFLD:			return mgArgErr;
		case DE_OPCANCELLED:			return cancelError;
		case DE_ACCESSDENIEDSRC:
		case DE_DEST_IS_CDROM:
		case DE_DEST_IS_DVD:
		case DE_DEST_IS_CDRECORD:
		case DE_ROOTDIR | ERRORONDEST:	return fNoPerm;
		case DE_PATHTOODEEP:
		case DE_INVALIDFILES:
		case DE_FILENAMETOOLONG:
		case DE_SRC_IS_CDROM:
		case DE_SRC_IS_DVD:
		case DE_SRC_IS_CDRECORD:		return fNotFound;
		case DE_ERROR_MAX:				return fDiskFull;
		case DE_FILE_TOO_LARGE:			return mgNotSupported;
		case ERRORONDEST:				return fIOErr;
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

static uInt32 LVFileFlagsFromWinFlags(DWORD dwAttrs)
{
	uInt32 type = 0;
	if (dwAttrs != INVALID_FILE_ATTRIBUTES)
	{
		if (dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT)
			type |= kIsLink;
		if (!(dwAttrs & FILE_ATTRIBUTE_DIRECTORY))
			type |= kIsFile;
		if (dwAttrs & FILE_ATTRIBUTE_HIDDEN)
		    type |= kFIsInvisible;
		if (dwAttrs & FILE_ATTRIBUTE_COMPRESSED)
			type |= kIsCompressed;
	}
	else
	{
		type = kErrGettingType;
	}
	return type;
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
	WIN32_FIND_DATALW fileData;
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
							if ((resolveDepth < 0 || resolveDepth >= *resolveCount) && Win32CanBeShortCutLink(tempPath, len))
							{
								srcPath = tempPath;
								continue;
							}
						}
						if (*dwAttrs == INVALID_FILE_ATTRIBUTES)
							*dwAttrs = GetFileAttributesW(tempPath);
						
						if (wTgt && LWPtrToLWPath(tempPath, len, wTgt, 0))
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

LibAPI(MgErr) Win32ResolveReparsePoint(LWPathHandle wSrc, LWPathHandle *pwTgt, int32 resolveDepth, int32 *resolveCount, DWORD *dwAttrs)
{
	HANDLE handle;
	int32 count = resolveCount ? *resolveCount : 0;
	MgErr err = noErr;
	DWORD bytes = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
	PREPARSE_DATA_BUFFER buffer = NULL;
	LWPathHandle wIntermediate = pwTgt ? *pwTgt : NULL;

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
			int32 length = 0;
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
					err = mgArgErr;
					break;
			}
			if (length)
			{
				int32 offset;
				count++;

				if (relative)
				{
					err = LWPathAppend(wSrc, LWPtrParent(LWPathBuf(wSrc), LWPathLenGet(wSrc), 0), &wIntermediate, start, length);
				}
				else
				{
					offset = HasNTFSSessionPrefix(start, length);
					if (!err && length >= 4 && offset  == 4)
					{
						wcsncpy(start, L"\\\\?\\", 4);
					}
					err = LWPtrToLWPath(start + offset, length - offset, &wIntermediate, 0);
				}
				
				if (!err)
				{
					*dwAttrs = GetFileAttributesLW(wIntermediate);
					if (*dwAttrs == INVALID_FILE_ATTRIBUTES)
					{
						*dwAttrs = FILE_ATTRIBUTE_NORMAL;
					}
					else if (!(*dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT) && relative)
					{
						offset = (start[0] == '.' && start[1] == '\\') ? 2 : 0;
						err = LWPtrToLWPath(start + offset, length - offset, &wIntermediate, 0);
					}
				}
			}
			else
			{
				err = fIOErr;
			}
		}
		wSrc = wIntermediate;
	} while (!err && (*dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT) && (resolveDepth < 0 || resolveDepth >= count));

	Win32ModifyBackupPrivilege(FALSE);
	DSDisposePtr((UPtr)buffer);
	if (!err && pwTgt)
	{
		*pwTgt = wIntermediate;
	}
	else
	{
		LWPathDispose(&wIntermediate);
	}
	if (resolveCount)
		*resolveCount += count;
	return err;
}
#endif
#elif Unix || MacOSX
/* seconds between Jan 1 1904 GMT and Jan 1 1970 GMT */
#define dt1970re1904    2082844800UL

#if VxWorks
// on VxWorks the stat time values are unsigned long integer
static void VxWorksConvertFromATime(ATime128 *time, unsigned long *sTime)
{
	/* VxWorks uses unsigned integers and can't represent dates before Jan 1, 1970 */
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
// on Mac/Linux kernel 2.6 and newer the utimes values are struct timeval values
static void UnixConvertFromATime(ATime128 *time, struct timespec *sTime)
{
	/* The LabVIEW default default value is used to indicate to not update this value */
	if (time->u.f.val || time->u.f.fract)
	{
		sTime->tv_sec = (time_t)(time->u.f.val - dt1970re1904);
		sTime->tv_nsec = (int32_t)(time->u.f.fract / 18446744074ULL);
	}
	else
	{
		sTime->tv_nsec = UTIME_OMIT;
	}
}

// on MacOSX/Linux kernel 2.6 and newer the stat time values are struct timespec values
static void UnixConvertToATime(struct timespec *sTime, ATime128 *time)
{
	time->u.f.val = (int64_t)sTime->tv_sec + dt1970re1904;
	time->u.f.fract = (uint64_t)sTime->tv_nsec * 18446744074ULL;
}
#endif

MgErr UnixToLVFileErr(void)
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
	  case EILSEQ:      return bogusError;
	  case E2BIG:       return mFullErr;
    }
    return fIOErr;   /* fIOErr generally signifies some unknown file error */
}

static uInt16 LVFileFlagsFromDType(uInt8 dType)
{
	uInt16 flags = 0;
	switch (dType)
	{
		case DT_REG:
			flags = kIsFile;
			break;
		case DT_LNK:
			flags = kIsLink;
			break;
		case DT_DIR:
			flags = 0;
			break;
		default:
			flags = kErrGettingType;
			break;
	}
	return flags;
}

static uInt32 LVFileFlagsFromStat(struct stat *statbuf)
{
	uInt32 flags = 0;
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

static MgErr MacGetFileTypeAndCreator(const char *path, ResType *type, ResType *creator)
{
	struct attrlist alist;
	fileinfobuf finfo;
	finderinfo *finder = (finderinfo*)(&finfo.data);

	bzero(&alist, sizeof(struct attrlist));
	alist.bitmapcount = ATTR_BIT_MAP_COUNT;
	alist.commonattr = ATTR_CMN_FNDRINFO;
	if (!getattrlist(path, &alist, &finfo, sizeof(fileinfobuf), FSOPT_NOFOLLOW))
	{
		if (type)
			*type = ConvertBE32(finder.type);
		if (creator)
			*creator = ConvertBE32(finder.creator);
		return mgNoErr;
	}
	return mgNotSupported;
}
#endif
#endif

#if !BigEndian
#define toLittle 1
#define toBig	 2

static void EndianizeRsrcHdr(RsrcHeader *rheader, int16 direction)
{
	Unused(direction);
	StdToW(&rheader->version);
	StdToL(&rheader->mapOffset);
	StdToL(&rheader->mapLength);
	StdToL(&rheader->dataOffset);
	StdToL(&rheader->dataLength);
}

static void EndianizeRsrcMap(RsrcMapPtr m, int16 direction)
{
	TypeMap *t = NULL;
	int32 numTypes, i, j, n;
	RsrcList *rl;

	if (direction == toBig)
	{
		t = TypeMapPtr(m);
	}

	EndianizeRsrcHdr((RsrcHeader *)m, direction);
	StdToL(&m->rsrcRefnum);
	StdToL(&m->rsrcFileAttr);
	StdToL(&m->typeOffset);
	StdToL(&m->nameOffset);

	if (direction == toLittle)
	{
		t = TypeMapPtr(m);
	}
	if (direction == toLittle)
	{
		StdToL(&t->numTypes);
	}
	numTypes = t->numTypes;
	if (direction == toBig)
	{
		StdToL(&t->numTypes);
	}

	for (i = 0; i <= numTypes; i++)
	{
		if (direction == toLittle)
		{
			StdToL(&t->type[i].numRsrcs);
		}
		n = t->type[i].numRsrcs;
		if (direction == toBig)
		{
			StdToL(&t->type[i].numRsrcs);
		}
		if (direction == toLittle)
		{
			StdToL(&t->type[i].listOffset);
		}
		rl = (RsrcList *)((UPtr)t + t->type[i].listOffset);
		if (direction == toBig)
		{
			StdToL(&t->type[i].listOffset);
		}
		for (j = 0; j <= n; j++)
		{
			StdToL(&rl->rsrcID);
			StdToL(&rl->nameOffset);
			StdToL(&rl->rsrcAttr);
			StdToL(&rl->dataOffset);
			rl++;
		}
	}
}
#else
#define EndianizeRsrcHdr(rheader, direction)
#define EndianizeRsrcMap(m, direction)
#endif

static MgErr lvFile_OpenResFile(LWPathHandle pathName, RsrcHeaderPtr rsrcHdr, FileRefNum *refnum)
{
	MgErr err = lvFile_OpenFile(refnum, pathName, kOpenFileRsrcData, openNormal, accessReadOnly, denyWriteOnly, 0);
	if (!err)
	{
		err = lvFile_SetFilePos(*refnum, 0, fStart);
		if (!err)
		{
			uInt32 actCount = 0;
			
			rsrcHdr->magic = 0;
			err = lvFile_Read(*refnum, sizeof(RsrcHeader), &actCount, (UPtr)rsrcHdr);
			if (!err)
			{
				err = rFNotFound;
				if (rsrcHdr->magic == RSRC_MAGIC)
				{
					StdToW(&rsrcHdr->version);
					if (actCount >= sizeof(RsrcOldHeader) && rsrcHdr->isCorrupt == 0 && rsrcHdr->version == 1)
					{
						/* If it is a VERY old resource file, convert the resource header to the new format */
						RsrcOldHeader *oheader = (RsrcOldHeader *)rsrcHdr;
						rsrcHdr->isCorrupt = NOT_CORRUPT;
						rsrcHdr->version = 3;
						rsrcHdr->dataLength = oheader->dataLength;
						rsrcHdr->dataOffset = oheader->dataOffset;
						rsrcHdr->mapLength = oheader->mapLength;
						rsrcHdr->mapOffset = oheader->mapOffset;
						rsrcHdr->type = kUnknownFileType;
						rsrcHdr->creator = kLVCreatorType;
						err = mgNoErr;
					}
					else if (actCount == sizeof(RsrcHeader))
					{
						if (rsrcHdr->isCorrupt == 0 && rsrcHdr->version == 2)
						{
							rsrcHdr->isCorrupt = NOT_CORRUPT;
							rsrcHdr->version = 3;
						}
						if (rsrcHdr->isCorrupt == NOT_CORRUPT && rsrcHdr->version >= 3)
						{
							err = mgNoErr;
						}
					}
					if (!err)
					{
						StdToL(&rsrcHdr->mapOffset);
						StdToL(&rsrcHdr->mapLength);
						StdToL(&rsrcHdr->dataOffset);
						StdToL(&rsrcHdr->dataLength);
					}
				}
			}
		}
		if (err)
		{
			lvFile_CloseFile(*refnum);
		}
	}
	return err;
}

static MgErr lvFile_GetFileTypeAndCreator(LWPathHandle pathName, ResType *type, ResType *creator)
{
	FileRefNum refnum = kInvalidFileRefnum;
	RsrcHeader rsrcHdr;
	MgErr err = lvFile_OpenResFile(pathName, &rsrcHdr, &refnum);
	if (!err)
	{
		if (rsrcHdr.magic == RSRC_MAGIC)
		{
			if (type)
				*type = rsrcHdr.type;
			if (creator)
				*creator = rsrcHdr.creator;
		}
		lvFile_CloseFile(refnum);
	}
#if MacOSX
	if (err)
	{
		err = MacGetFileTypeAndCreator(LWPathBuf(pathName), type, creator);
	}
#endif
	if (err)
	{
		/* We couldn't get the file type and creator from its internal resource format, so try to guess based on file extension */
		err = LWPathGetFileTypeAndCreator(pathName, type, creator);
	}
	return err;
}

static MgErr lvFile_LoadRsrcFromListPtr(FileRefNum refnum, RsrcMapPtr rm, RsrcList *rl)
{
	MgErr err = fEOF;

	if ((rl->dataOffset + sizeof(int32)) <= (uInt32)rm->dataLength)
	{
		int32 rsrcSize;
		UHandle h;
		
		err = lvFile_SetFilePos(refnum, rm->dataOffset + rl->dataOffset, fStart);
		if (err)
		{
			return err;
		}
		err = lvFile_Read(refnum, sizeof(int32), NULL, (UPtr)&rsrcSize);
		if (err)
		{
			return err;
		}
#if !BigEndian
		StdToL(&rsrcSize);
#endif
		if ((rl->dataOffset + sizeof(int32) + rsrcSize) > (uInt32)rm->dataLength)
		{
			return fEOF;
		}
		h = DSNewHandle(rsrcSize);
		if (!h)
		{
			return mFullErr;
		}
		err = lvFile_Read(refnum, rsrcSize, NULL, *h);
		if (!err)
		{
			rl->dataHdl = h;
		}
	}
	return err;
}

static TypeList *lvFile_FindTypeList(TypeMap *tm, ResType theType)
{
	int32 numTypes, i;
	TypeList *t;
	numTypes = tm->numTypes;
	t = &(tm->type[0]);

	for (i = 0; i <= numTypes; i++, t++)
	{
		if (t->rsrcType == theType)
		{
			return t;
		}
	}
	return NULL;
}

static int32 SearchID(int32 *resID, RsrcList *rl)
{
	return *resID - rl->rsrcID;
}

static RsrcList *lvFile_FindRsrcList(TypeMap *tm, TypeList *tl, RsrcID resID)
{
	int32 i, numRsrcs = tl->numRsrcs + 1;
	RsrcList *rl = (RsrcList *)((UPtr)tm + tl->listOffset);
	
	i = BinSearch((ConstUPtr)rl, numRsrcs, sizeof(RsrcList), (UPtr)&resID, (CompareProcPtr)SearchID);
	if (i < 0)
	{
		rl = NULL;
	}
	else
	{
		rl += i;
	}
	return rl;
}

static MgErr lvFile_GetResource(FileRefNum refnum, RsrcMapPtr rm, ResType resType, RsrcID resID, UHandle *h)
{
	TypeMap *tm = TypeMapPtr(rm);
	TypeList *tl;
	RsrcList *rl;
	MgErr err = rNotFound;

	*h = NULL;

	if (rm->mapLength <= sizeof(RsrcHeader))
	{
		return err;
	}

	tl = lvFile_FindTypeList(tm, resType);
	if (tl == (TypeList *)NULL)
	{
		return err;			/* no rsrcs of that type */
	}

	rl = lvFile_FindRsrcList(tm, tl, resID);
	if (rl)
	{
		if (rl->dataHdl == NULL)  	/* not in memory */
		{
			err = lvFile_LoadRsrcFromListPtr(refnum, rm, rl);
		}
		*h = rl->dataHdl;
	}
	return err;
}

static MgErr lvFile_ReadRsrcMap(FileRefNum refnum, RsrcHeaderPtr rsrcHeader, RsrcMapPtr *rMap)
{
	RsrcMapPtr ptr;
	MgErr err = noErr;
	if (rsrcHeader->mapLength != 0)
	{
		ptr = (RsrcMapPtr)DSNewPtr(rsrcHeader->mapLength);
		if (!ptr)
		{
			return mFullErr;
		}
		err = lvFile_SetFilePos(refnum, rsrcHeader->mapOffset, fStart);
		if (!err)
		{
			err = lvFile_Read(refnum, rsrcHeader->mapLength, NULL, (UPtr)ptr);
		}

#if !BigEndian
		EndianizeRsrcMap(ptr, toLittle);
#endif
	}
	else
	{
		ptr = (RsrcMapPtr)DSNewPtr(sizeof(RsrcMap));
		if (!ptr)
		{
			return mFullErr;
		}
		MoveBlock(rsrcHeader, ptr, sizeof(RsrcHeader));
		ptr->typeOffset = sizeof(RsrcMap);
		ptr->nameOffset = 0;
	}
	*rMap = ptr;
	return err;
}

static int16 tdClust2U32[]  = {14, clustCode, 2, 4, uL, 4, uL};
int16 *gClust2U32TD = tdClust2U32;
static int16 tdString[]  = {8, stringCode, -1, -1};
int16 *gStringTD = tdString;

static MgErr ResizeHandles(LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, int32 size)
{
	MgErr err = noErr;
	int32 oldElm = *nameArr ? (**nameArr)->numItems : 0,
	      newElm = (size > 0) ? size : 0;
	      
	if (newElm < oldElm)
	{
		int32 i = NameArrSize(*nameArr);
		LStrHandle *strPtr = (**nameArr)->elm + newElm;

		for (i = newElm; i < oldElm; i++, strPtr++)
		{
			if (*strPtr)
			{
				DSDisposeHandle((UHandle)*strPtr);
				*strPtr = NULL;
			}
		}
		if (size < 0)
		{
			DSDisposeHandle((UHandle)*nameArr);
			*nameArr = NULL;
		}
		else if (*nameArr)
		{
			(**nameArr)->numItems = size;
		}
	}
	else if (newElm > NameArrSize(*nameArr))
	{
		err = SetArraySize(&gStringTD, 0, 1, (UHandle*)nameArr, size);
	}

	if (!err)
	{
		if (size < 0)
		{
			DSDisposeHandle((UHandle)*typeArr);
			*typeArr = NULL;
		}
		else if (newElm > TypeArrSize(*typeArr))
		{
			err = SetArraySize(&gClust2U32TD, 0, 1, (UHandle*)typeArr, size);
		}
		else if (*typeArr)
		{
			(**typeArr)->numItems = size;
		}
	}
	return err;
}

static MgErr lvFile_FileFlags(LWPathHandle lwstr, int32 end, uInt32 *fileFlags)
{
	MgErr err = mgNoErr;
	LWChar ch = 0;

	if (!LWPathBuf(lwstr))
		return mgArgErr;

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
		if (!err && fileFlags)
			*fileFlags = LVFileFlagsFromWinFlags(dwAttr);
#else
		struct stat statbuf;
		char *path = SStrBuf(lwstr);

		errno = 0;
		if (lstat(path, &statbuf) != 0)
			err = UnixToLVFileErr();
		if (!err && fileFlags)
			*fileFlags = LVFileFlagsFromStat(&statbuf);
#endif
	}
	if (end > 0)
	{
		LWPathBuf(lwstr)[end] = ch;
	}
	return err;
}

static MgErr lvFile_ListArchive(LWPathHandle pathName, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 llbMode)
{
	FileRefNum refnum = kInvalidFileRefnum;
	RsrcHeader rsrcHeader;
	MgErr err = lvFile_OpenResFile(pathName, &rsrcHeader, &refnum);
	if (!err)
	{
		if (rsrcHeader.creator == kLVCreatorType && rsrcHeader.type == kArcFileType)
		{
			RsrcMapPtr rm;

			err = lvFile_ReadRsrcMap(refnum, &rsrcHeader, &rm);
			if (!err)
			{
				ADir **adir = NULL;

				err = lvFile_GetResource(refnum, rm, aDirResource, 0, (UHandle *)&adir);
				if (!err)
				{
					ADEntry *ap = (*adir)->e;
					FileTypePtr typePtr;
					LStrHandle *namePtr;

					int32 i, size = TypeArrSize(*typeArr), index = TypeArrSize(*typeArr);

					for (i = 0; !err && i < (*adir)->h.n; i++, index++)
					{
						if (llbMode != kListArchiveTopLvl || ap->flags & kIsTopLevelVI)
						{
							if (index >= size)
							{
								size = size >= 16 ? size * 2 : 16;
								err = ResizeHandles(nameArr, typeArr, size);
							}
							if (!err)
							{
								typePtr = (**typeArr)->elm + index;
								
								typePtr->flags = kIsFile | kRecognizedType | ap->flags & kIsTopLevelVI;
								typePtr->type = ap->type;
								namePtr = (**nameArr)->elm + index;

								err = ConvertCString(PStrBuf(ap->name), PStrLen(ap->name), CP_ACP, namePtr, CP_UTF8, 0, NULL);
							}
						}
						ap = NextADEntry(ap);
					}
					DSDisposeHandle((UHandle)adir);
				}
			}
		}
		lvFile_CloseFile(refnum);
	}
	return err;
}

/* Internal list directory function
   On Windows the pathName is a wide char Long String pointer and the function returns UTF8 encoded filenames in the name array
   On other platforms it uses whatever is the default encoding for both pathName and the filenames, which could be UTF8 (Linux and Mac)
 */
static MgErr lvFile_ListDirectory(LWPathHandle pathName, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 llbMode, int32 resolveDepth)
{
	MgErr err = noErr;
	uInt32 fileFlags = 0;
	int32 pathLen = LWPathLenGet(pathName),
		  pathCnt = LWPathCntGet(pathName),
		  index = TypeArrItems(*typeArr),
		  size = TypeArrSize(*typeArr);
	ResType creator = 0, fileType = 0;
	LStrHandle *namePtr = NULL;
	FileTypePtr typePtr = NULL;
#if Win32
	HANDLE dirp = INVALID_HANDLE_VALUE;
	DWORD ret;
	LWChar *fileName = NULL;
	WIN32_FIND_DATALW fileData;
#if !Pharlap
	PVOID oldRedirection = NULL;
	LWPathHandle wTgt = NULL;
#endif
#endif

	if (!pathLen)
	{
		//Empty path is the filesystem root
#if usesWinPath
		// Enumerate the disk drive volumes
		DWORD slen = GetLogicalDriveStringsLW(0, NULL);
		if (slen)
		{
			LWChar *buf = (LWChar *)DSNewPClr((slen + 1) * sizeof(LWChar));
			if (!buf)
			{
				return mFullErr;
			}
			slen = GetLogicalDriveStringsLW(slen, buf);
			if (slen)
			{
				int32 len, off = 0;

				while (!err && buf[off])
				{
					if (index >= size)
					{
						size = size >= 16 ? size * 2 : 16;
						err = ResizeHandles(nameArr, typeArr, size);
					}
					if (!err)
					{
						namePtr = (**nameArr)->elm + index;
						typePtr = (**typeArr)->elm + index;

						typePtr->flags = 0;
						typePtr->type = 0;
#if Pharlap
						len = StrLen(buf + off);
						err = ConvertCString(buf + off, len, CP_ACP, namePtr, CP_UTF8, 0, NULL);
#else
						len = (int32)wcslen(buf + off);
						err = WideCStrToMultiByte(buf + off, len, namePtr, 0, CP_UTF8, 0, NULL);
#endif
						off += len + 1;
						index++;
						(**nameArr)->numItems = index;
						(**typeArr)->numItems = index;
					}
				}
			}
			DSDisposePtr((UPtr)buf);
		}
		return err;
#elif usesHFSPath
		// Need to enumerate the desktop
		return mgUnsupported;
#elif usesPosixPath
		// Fall through to normal enumeration
		err = LWPathAppendSeperator(pathName, 0);
		if (!err)
			pathLen = LWPathLenGet(pathName);
#endif
	}
	else
	{
		err = lvFile_FileFlags(pathName, pathLen, &fileFlags);
		if (!err && fileFlags & kIsFile)
		{
			if (llbMode)
			{
				return lvFile_ListArchive(pathName, nameArr, typeArr, llbMode);
			}
			return mgArgErr;
		}
	}

#if Win32
	if (!err)
	{
		err = LWPathAppendSeperator(pathName, pathLen);
		if (!err)
		{
			pathLen = LWPathLenGet(pathName);
			err = LWPathNCat(&pathName, pathLen, strWildcardPattern, 3);
		}
	}
	if (!err)
	{
#if !Pharlap
		if (!Wow64DisableWow64FsRedirection(&oldRedirection))
		{
			ret = GetLastError();
			/* Failed but lets go on anyhow, risking strange results for virtualized paths */
			oldRedirection = NULL;
		}
#endif
		dirp = FindFirstFileLW(pathName, &fileData);
		if (dirp != INVALID_HANDLE_VALUE)
		{
			do
			{
				/* Skip the current dir, and parent dir entries */
				if (fileData.cFileName[0] != 0 && (fileData.cFileName[0] != '.' || (fileData.cFileName[1] != 0 && (fileData.cFileName[1] != '.' || fileData.cFileName[2] != 0))))
				{
					/* Make sure our arrays are resized to allow the new values */
					if (index >= size)
					{
						size = size >= 16 ? size * 2 : 16;
						err = ResizeHandles(nameArr, typeArr, size);
					}
					if (!err)
					{
						namePtr = (**nameArr)->elm + index;
						typePtr = (**typeArr)->elm + index;
						fileName = fileData.cFileName;

						if (fileData.dwFileAttributes != INVALID_FILE_ATTRIBUTES)
						{
							fileFlags = LVFileFlagsFromWinFlags(fileData.dwFileAttributes);
							if (fileFlags & (kIsLink | kIsFile))
							{
								/* Create the path to the file for intermediate operations */
								err = LWPathNCat(&pathName, pathLen, fileName, -1);
								if (err)
									break;
#if !Pharlap
								if ((fileFlags & kIsLink) && resolveDepth)
								{
									err = lvFile_ReadLink(pathName, &wTgt, resolveDepth, NULL, &fileFlags);
									if (err)
										break;
								}
#endif
								if (fileFlags & kIsFile)
								{
									err = lvFile_GetFileTypeAndCreator(pathName, &fileType, &creator);
									if (!err)
									{
										fileFlags |= kRecognizedType;
										if (creator == kLVCreatorType && fileType == kArcFileType)
										{
											fileFlags |= kIsArchive;
										}
									}
								}
							}
						}
						else
						{
							/* can happen if file disappeared since we did FindNextFile */
							fileFlags = kErrGettingType | kIsFile;
						}
						typePtr->flags = fileFlags;
						typePtr->type = fileType;
#if Pharlap
						err = ConvertCString(fileName, -1, CP_ACP, namePtr, CP_UTF8, 0, NULL);
#else
						err = WideCStrToMultiByte(fileName, -1, namePtr, 0, CP_UTF8, 0, NULL);
#endif
						index++;
						(**nameArr)->numItems = index;
						(**typeArr)->numItems = index;
					}
				}
			}
			while (!err && FindNextFileLW(dirp, &fileData));
			FindClose(dirp);
		}
		else
		{
			ret = GetLastError();
			if (ret != ERROR_FILE_NOT_FOUND)
				err = Win32ToLVFileErr(ret);
		}
#if !Pharlap
		LWPathDispose(&wTgt);
		if (oldRedirection)
			Wow64RevertWow64FsRedirection(&oldRedirection);
#endif
	}
#else // Unix, VxWorks, and MacOSX
	if (!err)
	{
		char *path = LWPathBuf(pathName);
		DIR *dirp = opendir(path);
		if (dirp)
		{
			struct stat statbuf;
			struct dirent *dp;

			err = LWPathAppendSeperator(pathName, pathLen);
			if (!err)
			{
				pathLen = LWPathLenGet(pathName);
			}

			for (dp = readdir(dirp); !err && dp; dp = readdir(dirp))
			{
				/* Skip the current dir, and parent dir entries. They are not guaranteed to be always the first
				   two entries enumerated! */
				if (dp->d_name[0] != '.' || (dp->d_name[1] != 0 && (dp->d_name[1] != '.' || dp->d_name[2] != 0)))
				{
					if (index >= size)
					{
						size = size >= 16 ? size * 2 : 16;
						err = ResizeHandles(nameArr, typeArr, size);
					}
					if (!err)
					{
						namePtr = (**nameArr)->elm + index;
						typePtr = (**typeArr)->elm + index;

						err = ConvertCString((ConstCStr)dp->d_name, -1, CP_ACP, namePtr, CP_UTF8, 0, NULL);
						if (err)
							break;
			
						err = LWPathNCat(&pathName, pathLen, dp->d_name, -1);
						if (err)
							break;
							
						fileFlags = kErrGettingType;
						path = LWPathBuf(pathName);
#ifdef _DIRENT_HAVE_D_TYPE
						if (dp->d_type != DT_UNKNOWN)
						{
							fileFlags = LVFileFlagsFromDType(dp->d_type);
						}
						else
#endif
						if (!lstat(path, &statbuf))
						{
							fileFlags = LVFileFlagsFromStat(&statbuf);
						}
						if (!(fileFlags & kErrGettingType))
						{
#if !VxWorks				/* VxWorks does not support links */
							if (fileFlags & kIsLink && resolveDepth)
							{
								if (stat(path, &statbuf))					/* If link points to something */
								{
									fileFlags |= kErrGettingType;
								}
								else
								{
									fileFlags |= LVFileFlagsFromStat(&statbuf);	/* return also info about it not link. */
								}
							}
#endif
							if (fileFlags & kIsFile)
							{
								err = lvFile_GetFileTypeAndCreator(pathName, &fileType, &creator);
								if (!err)
								{
									fileFlags |= kRecognizedType;
									if (creator == kLVCreatorType && fileType == kArcFileType)
									{
										fileFlags |= kIsArchive;
									}
								}
							}
						}
						typePtr->flags = fileFlags;
						typePtr->type = fileType;
					}
					index++;
					(**nameArr)->numItems = index;
					(**typeArr)->numItems = index;
				}
			}
			closedir(dirp);
		}
	}
	else
	{
		err = UnixToLVFileErr();
	}
#endif
	if (!err)
	{
		if (LWPathBuf(pathName)[pathLen - 1] == kPathSeperator)
			pathLen--;
		LWPathLenSet(pathName, Max(LWPtrRootLen(LWPathBuf(pathName), pathLen, -1, NULL), pathLen));
		LWPathCntSet(pathName, pathCnt);
	}
	return err;
}

LibAPI(MgErr) LVFile_ListDirectory(LWPathHandle *folderPath, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 llbMode, int32 resolveDepth)
{
	MgErr err = noErr;
	LWPathHandle tempPath = NULL;

	if (!nameArr || !typeArr)
		return mgArgErr;

	err	= LWPathCopy(&tempPath, *folderPath);
	if (!err)
	{
		err = ResizeHandles(nameArr, typeArr, 16);
		if (!err)
		{
			(**nameArr)->numItems = 0;
			(**typeArr)->numItems = 0;

			err = lvFile_ListDirectory(tempPath, nameArr, typeArr, llbMode, resolveDepth);
		}
		LWPathDispose(&tempPath);
	}
	return err;
}

static MgErr lvFile_HasResourceFork(LWPathHandle pathName, LVBoolean *hasResFork, FileOffset *size)
{
    MgErr  err = mgNoErr;
#if !MacOSX
    Unused(pathName);
#else
	FileOffset offset;
#endif
    if (hasResFork)
	    *hasResFork = LV_FALSE;
	if (size)
		size->q = 0;

#if MacOSX
	err = MacGetResourceSize(LWPathBuf(lwstr), &offset.q);
	if (!err)
	{
        if (hasResFork && offset.q)
            *hasResFork = LV_TRUE;
        if (size)
            size->q = offset.q;
    }
#endif
    return err;
}

LibAPI(MgErr) LVFile_HasResourceFork(LWPathHandle *pathName, LVBoolean *hasResFork, FileOffset *size)
{
	MgErr err = LWPathZeroTerminate(pathName, -1);
	if (!err)
		err = lvFile_HasResourceFork(*pathName, hasResFork, size);
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

#define kSetableWinFileFlags (kWinFileInfoArchive | kWinFileInfoHidden | kWinFileInfoNotIndexed | kWinFileInfoOffline | \
				              kWinFileInfoReadOnly | kWinFileInfoSystem | kWinFileInfoTemporary)

static uInt16 UnixFlagsFromWindows(uInt16 attr)
{
	uInt16 flags = attr & kWinFileInfoReadOnly ?  0444 : 0666;
	if (attr & kWinFileInfoReparsePoint)
		flags |= S_IFLNK;
	else if (attr & kWinFileInfoDirectory)
		flags |= S_IFDIR;
	else if (attr & kWinFileInfoDevice)
		flags |= S_IFCHR;
	else
		flags |= S_IFREG; 
	return flags;
}

static uInt16 WinFlagsFromUnix(uInt32 mode)
{
	uInt16 flags = mode & 0222 ? 0 : kWinFileInfoReadOnly;
	switch (mode & S_IFMT)
	{
	     case S_IFDIR:
			flags |= kWinFileInfoDirectory;
			break;
	     case S_IFCHR:
	     case S_IFBLK:
			flags |= kWinFileInfoDevice;
			break;
	     case S_IFLNK:
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

static MgErr lvFile_FileInfo(LWPathHandle pathName, uInt8 write, FileInfoPtr fileInfo)
{
	MgErr err = noErr;
	uInt8 type = LWPathTypeGet(pathName);
	uInt16 cnt = LWPathCntGet(pathName);
	uInt64 count = 0;
#if Win32 // Windows
    HANDLE handle = NULL;
	WIN32_FIND_DATALW fi = {0};
#else // Unix, VxWorks, and MacOSX
	char *path = NULL;
    struct stat statbuf;
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
		fi.dwFileAttributes = !cnt ? FILE_ATTRIBUTE_DIRECTORY : GetFileAttributesLW(pathName);
    }
    else
    {
		handle = FindFirstFileLW(pathName, &fi);
		if ((handle == INVALID_HANDLE_VALUE) || !FindClose(handle))
		{
			err = Win32GetLVFileErr();
		}
    }

    if (!err)
    {
		if (write)
		{
			if (fileInfo->cDate.u.f.val)
				ATimeToFileTime(&fileInfo->cDate, (uInt64*)&fi.ftCreationTime);
			if (fileInfo->mDate.u.f.val)
				ATimeToFileTime(&fileInfo->mDate, (uInt64*)&fi.ftLastWriteTime);
			if (fileInfo->aDate.u.f.val)
				ATimeToFileTime(&fileInfo->aDate, (uInt64*)&fi.ftLastAccessTime);
			handle = CreateFileLW(pathName, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, NULL);
			if (handle != INVALID_HANDLE_VALUE)
			{
			    if (!SetFileTime(handle, &fi.ftCreationTime, &fi.ftLastAccessTime, &fi.ftLastWriteTime))
				    err = Win32GetLVFileErr();
				CloseHandle(handle);
			}
			else
				err = Win32GetLVFileErr();

			if (!err && fi.dwFileAttributes != INVALID_FILE_ATTRIBUTES)
			{
				if (!fileInfo->winFlags && fileInfo->unixFlags)
					fileInfo->winFlags = WinFlagsFromUnix(fileInfo->unixFlags);

				fi.dwFileAttributes &= ~kSetableWinFileFlags;
				fi.dwFileAttributes |= fileInfo->winFlags & kSetableWinFileFlags;
                if (!fi.dwFileAttributes)
					fi.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
			    SetFileAttributesLW(pathName, fi.dwFileAttributes);
			}
		}
		else
		{
			fileInfo->uid = 0xFFFFFFFF;
			fileInfo->gid = 0xFFFFFFFF;
			FileTimeToATime((uInt64*)&fi.ftCreationTime, &fileInfo->cDate);
			FileTimeToATime((uInt64*)&fi.ftLastWriteTime, &fileInfo->mDate);
			FileTimeToATime((uInt64*)&fi.ftLastAccessTime, &fileInfo->aDate);
			fileInfo->rfSize = 0;
			fileInfo->winFlags = Lo16(fi.dwFileAttributes);
			fileInfo->unixFlags = UnixFlagsFromWindows(fileInfo->winFlags);
			fileInfo->macFlags = MacFlagsFromWindows(fileInfo->winFlags);
			fileInfo->fileType = LVFileFlagsFromWinFlags(fi.dwFileAttributes);

			if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				int32 len = LWPathLenGet(pathName);

				fileInfo->type = kUnknownFileType;
				fileInfo->creator = kUnknownCreator;

				if (!len)
				{
					DWORD drives = GetLogicalDrives();
					int drive;

	                for (drive = 1; drives && drive <= 26; drive++)
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
			        err = LWPathNCat(&pathName, len, strWildcardPattern, 3);
					if (!err)
					{
						handle = FindFirstFileLW(pathName, &fi);
						if (handle != INVALID_HANDLE_VALUE)
						{
							count = 1;
							while (FindNextFileLW(handle, &fi))
								count++;
							FindClose(handle);
						}
						LWPathLenSet(pathName, len);
						LWPathCntSet(pathName, cnt);
					}
				}
	            /* FindFirstFile doesn't enumerate . and .. entries in a volume root */
				if (cnt > 1)
				    fileInfo->size = count - 2;
				else
					fileInfo->size = count;
			}
			else
			{		
#if !Pharlap
				handle = CreateFileLW(pathName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
				if (handle != INVALID_HANDLE_VALUE)
				{
					FILE_STANDARD_INFO info;
					if (GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(FILE_STANDARD_INFO)))
					{
						fileInfo->nLink = info.NumberOfLinks;
					}
					CloseHandle(handle);
				}

				if (!(fileInfo->fileType & kIsLink))
				{
					err = Win32ResolveShortCut(pathName, NULL, 0, NULL, &fi.dwFileAttributes);
					if (!err)
						fileInfo->fileType |= kIsLink;
					else if (err == cancelError)
						err = mgNoErr;
				}
#endif
				if (!err && !(fileInfo->fileType & kIsLink))
				{
					if (!lvFile_GetFileTypeAndCreator(pathName, &fileInfo->type, &fileInfo->creator) && fileInfo->type && fileInfo->type != kUnknownFileType)
					{
						fileInfo->fileType |= kRecognizedType;
					}
				}
				fileInfo->size = Quad(fi.nFileSizeHigh, fi.nFileSizeLow);
			}
		}
    }
#else
	path = cnt ? LWPathBuf(pathName) : "/";
    if (lstat(path, &statbuf))
		return UnixToLVFileErr();

	if (write)
	{
		int ret;
#if VxWorks
		struct utimbuf times;
#else
		struct timespec times[3];
#endif

		if (!fileInfo->unixFlags)
			fileInfo->unixFlags = UnixFlagsFromWindows(fileInfo->winFlags);
		if (!fileInfo->macFlags)
			fileInfo->macFlags = MacFlagsFromWindows(fileInfo->winFlags);
#if VxWorks
		VxWorksConvertFromATime(&fileInfo->aDate, &times.actime);
		VxWorksConvertFromATime(&fileInfo->mDate, &times.modtime);
        VxWorksConvertFromATime(&fileInfo->cDate, &statbuf.st_ctime);
		ret = utime(LWPathBuf(pathName), &times);
#else
		UnixConvertFromATime(&fileInfo->aDate, &times[0]);
		UnixConvertFromATime(&fileInfo->mDate, &times[1]);
        UnixConvertFromATime(&fileInfo->cDate, &times[2]);
		ret = utimensat(0, path, times, AT_SYMLINK_NOFOLLOW);
#endif
		if (ret == -1)
		{
			err = UnixToLVFileErr();
		}
#if !VxWorks
		/*
		 * Changing the ownership probably won't succeed, unless we're root
         * or POSIX_CHOWN_RESTRICTED is not set.  Set uid/gid before setting
         * the mode; current BSD behavior is to remove all setuid bits on
         * chown. If chown fails, loose setuid/setgid bits.
         */
        else if (chown(path, fileInfo->uid, fileInfo->gid))
		{
	        if (errno != EPERM && errno != ENOTSUP)
				err = UnixToLVFileErr();
	        fileInfo->unixFlags &= ~(S_ISUID | S_ISGID);
        }
#endif
        if (!err && chmod(path, (statbuf.st_mode & 0170000) | (fileInfo->unixFlags & 07777)) && errno != ENOTSUP)
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
		fileInfo->fileType = LVFileFlagsFromStat(&statbuf);

		if (S_ISDIR(statbuf.st_mode))
		{
			DIR *dirp;
			struct dirent *dp;

			fileInfo->type = kUnknownFileType;
			fileInfo->creator = kUnknownCreator;

			if (!(dirp = opendir(path)))
				return UnixToLVFileErr();

			for (dp = readdir(dirp); dp; dp = readdir(dirp))
			{
				count++;
			}
			closedir(dirp);
			fileInfo->size = count - 2;
			fileInfo->rfSize = 0;
		}
		else
		{
			if (S_ISLNK(statbuf.st_mode))
			{
				fileInfo->fileType |= kIsLink;
			}
			else if (!lvFile_GetFileTypeAndCreator(pathName, &fileInfo->type, &fileInfo->creator) && fileInfo->type && fileInfo->type != kUnknownFileType)
			{
				fileInfo->fileType |= kRecognizedType;
			}
			fileInfo->size = statbuf.st_size;
			fileInfo->nLink = statbuf.st_nlink;
#if MacOSX
			MacGetResourceSize(path, &fileInfo->rfSize);
#else
			fileInfo->rfSize = 0;
#endif
		}
	}
#endif
    return err;
}

LibAPI(MgErr) LVFile_FileInfo(LWPathHandle *pathName, uInt8 write, FileInfoPtr fileInfo)
{
	LWPathHandle tempName = NULL;
	MgErr err = LWPathCopy(&tempName, *pathName);
	if (!err)
	{
		err = lvFile_FileInfo(tempName, write, fileInfo);
		LWPathDispose(&tempName);
	}
	return err;
}

#if Win32 && !Pharlap
/* Hardlinks are only valid for files */
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
			FreeLibrary(hLib);
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
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (0x2)
#endif

static BOOL Win32ControlSymlinkPriv(BOOL fEnable)
{
	HANDLE hToken;
	DWORD count = 0;
	BOOL result = TRUE;

	PTOKEN_PRIVILEGES lpTokenPriv = malloc(sizeof(TOKEN_PRIVILEGES) + 3 * sizeof(LUID_AND_ATTRIBUTES));

	if (LookupPrivilegeValue(NULL, SE_CREATE_SYMBOLIC_LINK_NAME, &lpTokenPriv->Privileges[count].Luid))
	{
		count++;
	}
	if (LookupPrivilegeValue(NULL, SE_BACKUP_NAME, &lpTokenPriv->Privileges[count].Luid))
	{
		count++;
	}
	if (LookupPrivilegeValue(NULL, SE_RESTORE_NAME, &lpTokenPriv->Privileges[count].Luid))
	{
		count++;
	}

	lpTokenPriv->PrivilegeCount = count;
	lpTokenPriv->Privileges[0].Attributes = fEnable ? SE_PRIVILEGE_ENABLED : 0;
	lpTokenPriv->Privileges[1].Attributes = fEnable ? SE_PRIVILEGE_ENABLED : 0;
	lpTokenPriv->Privileges[2].Attributes = fEnable ? SE_PRIVILEGE_ENABLED : 0;
	lpTokenPriv->Privileges[3].Attributes = fEnable ? SE_PRIVILEGE_ENABLED : 0;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		result = FALSE;
		goto exit;
	}
	result = AdjustTokenPrivileges(hToken, FALSE, lpTokenPriv, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS;
	CloseHandle(hToken);
exit:
	free(lpTokenPriv);
	return result;
}

typedef BOOL (WINAPI *tCreateSymbolicLink)(LPCWSTR lpSymlinkFileName, LPCWSTR lpTargetFileName, DWORD dwFlags);

static MgErr Win32CreateSymbolicLink(LWPathHandle lwSymlinkFileName, LWPathHandle lwTargetFileName, LPSECURITY_ATTRIBUTES lpsa, DWORD dwFlags)
{
	MgErr err = mgNotSupported;
	static tCreateSymbolicLink pCreateSymbolicLink = NULL;
	DWORD attrs = GetFileAttributesLW(lwTargetFileName);
	if (attrs != INVALID_FILE_ATTRIBUTES)
	{
		if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY)
			dwFlags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
		else
			dwFlags &= ~SYMBOLIC_LINK_FLAG_DIRECTORY;
	}
	
	if ((dwFlags & 0x8000) && !pCreateSymbolicLink)
	{
		HMODULE hLib = LoadLibrary("kernel32.dll");
		if (hLib)
		{
			pCreateSymbolicLink = (tCreateSymbolicLink)GetProcAddress(hLib, "CreateSymbolicLinkW");
			FreeLibrary(hLib);
		}
	}

	if ((dwFlags & 0x8000) && pCreateSymbolicLink)
	{
		if (!pCreateSymbolicLink(LWPathBuf(lwSymlinkFileName), LWPathBuf(lwTargetFileName), dwFlags))
			err = Win32GetLVFileErr();
		else
			err = noErr;
	}
	else
	{
		BOOL isRelative = LWPathTypeGet(lwTargetFileName) == fRelPath;
		HANDLE hFile;
		BOOL (WINAPI *deletefunc)() = DeleteFileW;
		DWORD openMode = CREATE_NEW, openFlags = FILE_FLAG_OPEN_REPARSE_POINT;

	    if (dwFlags & SYMBOLIC_LINK_FLAG_DIRECTORY)
		{
		    if (!CreateDirectoryLW(lwSymlinkFileName, lpsa))
			{
				return Win32GetLVFileErr();
			}
			openMode = OPEN_EXISTING;
			openFlags |= FILE_FLAG_BACKUP_SEMANTICS;
			deletefunc = RemoveDirectoryW;
		}
		hFile = CreateFileLW(lwSymlinkFileName, GENERIC_READ | GENERIC_WRITE, 0, lpsa, openMode, openFlags, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			int32 length = LWPathLenGet(lwTargetFileName);
			LPWSTR lpTargetFileName = LWPathBuf(lwTargetFileName);
			DWORD bytes = (DWORD)(REPARSE_DATA_SYMBOLIC_HEADER_SIZE + length * sizeof(WCHAR) * 2 + 20);
			PREPARSE_DATA_BUFFER buffer = (PREPARSE_DATA_BUFFER)DSNewPClr(bytes);
			if (buffer)
			{
				int32 prefix = 4, offset = HasNTFSDevicePrefix(lpTargetFileName, length);
				if (isRelative)
				{
					prefix = 2;
				}
				buffer->ReparseTag = IO_REPARSE_TAG_SYMLINK;
				buffer->Reserved = 0;
				buffer->SymbolicLinkReparseBuffer.PrintNameOffset = 0;
				buffer->SymbolicLinkReparseBuffer.PrintNameLength = (USHORT)((length - offset) * sizeof(WCHAR));
	            
				memcpy((char *)buffer->SymbolicLinkReparseBuffer.PathBuffer + buffer->SymbolicLinkReparseBuffer.PrintNameOffset,
		               lpTargetFileName + offset, buffer->SymbolicLinkReparseBuffer.PrintNameLength);

				buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset = buffer->SymbolicLinkReparseBuffer.PrintNameOffset + buffer->SymbolicLinkReparseBuffer.PrintNameLength;
				buffer->SymbolicLinkReparseBuffer.SubstituteNameLength = (USHORT)((length - offset + prefix) * sizeof(WCHAR));
	
				if (prefix == 2)
					memcpy((char *)buffer->SymbolicLinkReparseBuffer.PathBuffer + buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset, L".\\", prefix * sizeof(LWChar));
				else if (prefix == 4)
					memcpy((char *)buffer->SymbolicLinkReparseBuffer.PathBuffer + buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset, L"\\??\\", prefix * sizeof(LWChar));
				memcpy((char *)buffer->SymbolicLinkReparseBuffer.PathBuffer + buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset + prefix * sizeof(LWChar),
		               lpTargetFileName + offset, buffer->SymbolicLinkReparseBuffer.SubstituteNameLength - prefix * sizeof(LWChar));

				buffer->SymbolicLinkReparseBuffer.Flags = isRelative ? SYMLINK_FLAG_RELATIVE : 0;
				buffer->ReparseDataLength = REPARSE_DATA_SYMBOLIC_HEADER_SIZE - REPARSE_DATA_BUFFER_HEADER_SIZE + 
					                        buffer->SymbolicLinkReparseBuffer.PrintNameLength + buffer->SymbolicLinkReparseBuffer.SubstituteNameLength;
				
				bytes = REPARSE_DATA_BUFFER_HEADER_SIZE + buffer->ReparseDataLength;
				if (!DeviceIoControl(hFile, FSCTL_SET_REPARSE_POINT, buffer, bytes, NULL, 0, &bytes, NULL))
				{
				    err = Win32GetLVFileErr();
					deletefunc(LWPathBuf(lwSymlinkFileName));
				}
				else
				{
					err = mgNoErr;
				}
				DSDisposePtr((UPtr)buffer);
			}
			else
			{
				err = mFullErr;
			}
			CloseHandle(hFile);
		}
		else
		{
			err = Win32GetLVFileErr();
		}
	}
	return err;
}
#endif

static MgErr lvFile_CreateLink(LWPathHandle src, LWPathHandle tgt, uInt32 flags)
{
	int8 type = LWPathTypeGet(src); 
	if (type == fAbsPath || type == fUNCPath)
	{
#if MacOSX || Unix
		char *spath = LWPathBuf(src),
		     *tpath = LWPathBuf(tgt);
#endif
		MgErr err = lvFile_FileFlags(src, -1, NULL);
		if (!err || err != fNotFound)
			return fDupPath;
#if MacOSX || Unix
		err = mgNoErr;
		if (flags & kLinkHard)
		{
			/* create a hard link at location spath pointing at tpath */
			if (link(tpath, spath))
				err = UnixToLVFileErr();
		}
		else
		{
			/* create a symbolic link at location spath with the content of tpath */
			if (symlink(tpath, spath))
				err = UnixToLVFileErr();
		}
#elif Win32 && !Pharlap
		// Need to acquire privileges in order to be able to call symlink kernel entry points
		Win32ControlSymlinkPriv(TRUE);
		if (flags & kLinkHard)
		{
			err = Win32CreateHardLink(src, tgt, NULL);
		}
		else 
		{
			err = Win32CreateSymbolicLink(src, tgt, NULL, flags & kLinkDir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
		}
 		Win32ControlSymlinkPriv(FALSE);
#endif
		return err;
	}
	return mgArgErr;
}

LibAPI(MgErr) LVFile_CreateLink(LWPathHandle *src, LWPathHandle *tgt, uInt32 flags)
{
	MgErr err = LWPathZeroTerminate(src, -1);
	if (!err)
	{
		int32 type = LWPathTypeGet(*tgt);
		LWPathHandle tmp = NULL;
		if (type == fRelPath)
		{
			int32 len = LWPathLenGet(*tgt);
			LWChar *path = LWPathBuf(*tgt);
			int32 offset = LWPtrParent(path, len, 0);

			err = LWPathAppend(*src, offset, &tmp, path, len);
		}
		else
		{
			tmp = *tgt;
		}
		if (!err)
		{
			uInt32 fileFlags;
			if (!lvFile_FileFlags(tmp, -1, &fileFlags))
			{
				if (fileFlags == kIsFile)
					flags &= ~kLinkDir;
				else
					flags |= kLinkDir;
			}
			if (tmp != *tgt)
				LWPathDispose(&tmp);

			err = LWPathZeroTerminate(tgt, -1);
			if (!err)
				err = lvFile_CreateLink(*src, *tgt, flags);
		}
	}
	return err;
}

/* Read the path a link points to
   src: Path of the supposed link to read its destination
   tgt: Returned path with the location
   resolveDepth: kRecursive (-1), resolve the link over multiple levels if applicable
		         0, return relative paths if the link is relative
				 >0, maximum amount of resolutions
   resolveCount:
   fileFlags: the LV file flags of the returned location
   Returns: noErr on success resolution, cancelErr if the path is not a symlink, other errors as returned by the used API functions
*/
static MgErr lvFile_ReadLink(LWPathHandle pathName, LWPathHandle *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileAttr)
{
	MgErr err = mgNoErr;
	LWPathHandle src = pathName, tmp = NULL;
	int8 type = LWPathTypeGet(src); 
	int32 offset;
#if Win32 && !Pharlap
	DWORD dwAttrs;
#elif Unix || MacOSX
	struct stat statbuf;
	char *spath = SStrBuf(src), *tpath;
#endif

	if (type != fAbsPath && type != fUNCPath)
		return mgArgErr;

	if (resolveCount)
		*resolveCount = 0;

#if Win32 && !Pharlap
	dwAttrs = GetFileAttributesLW(pathName);
	if (dwAttrs == INVALID_FILE_ATTRIBUTES)
		return fNotFound;

	while (!err && (dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT || !(dwAttrs & FILE_ATTRIBUTE_DIRECTORY)))
	{
		if (dwAttrs & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			err = Win32ResolveReparsePoint(src, target, resolveDepth, resolveCount, &dwAttrs);
		}
		else
		{
			err = Win32ResolveShortCut(src, target, resolveDepth, resolveCount, &dwAttrs);
		}

		if (!err && (resolveDepth < 0 || (resolveCount && resolveDepth > *resolveCount)))
		{
			LWChar *tgtPtr = LWPathBuf(*target);
			int32 tgtLen = LWPathLenGet(*target);

			offset = 0;
			/* Is the link target a relative path? */
			if (LWPathTypeGet(*target) == fRelPath)
			{
				offset = LWPtrParent(LWPathBuf(src), LWPathLenGet(src), -1);
				offset++;
			}
			if (offset && src == pathName)
				err = LWPathNCat(&tmp, 0, LWPathBuf(src), offset);
			if (tgtLen)
				err = LWPathNCat(&tmp, offset, tgtPtr, tgtLen);

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
		{
			break;
		}
	}

	if (err == cancelError)
		err = mgNoErr;

	if (!err && fileAttr)
		*fileAttr |= LVFileFlagsFromWinFlags(dwAttrs);
#elif Unix || MacOSX
	if (lstat(spath, &statbuf))
		return fNotFound;

	while (!err && S_ISLNK(statbuf.st_mode))
	{
		do
		{
			offset = statbuf.st_size;
			err = LWPathResize(target, offset, 1);
		    if (!err)
			{
				tpath = SStrBuf(*target);
				statbuf.st_size = readlink(spath, tpath, offset + 1);
				if (statbuf.st_size < 0)
				{
					err = UnixToLVFileErr();
				}
			}
		}
		while (!err && statbuf.st_size > offset);

		if (!err)
		{
			int32 rootLen = LWPtrRootLen(LWPathBuf(*target), statbuf.st_size, 0, &type);
			int32 count = LWPtrDepth(LWPathBuf(*target), statbuf.st_size, rootLen, rootLen);
			LWPathTypeSet(*target, type);
			LWPathLenSet(*target, statbuf.st_size);
			LWPathCntSet(*target, count);
				
			if (resolveCount)
				(*resolveCount)++;
		}

		if (err || (resolveDepth >= 0 && resolveCount && resolveDepth <= *resolveCount))
			break;

		offset = 0;

		/* Is the link target a relative path? */
		if (LWPathBuf(*target)[0] != kPathSeperator)
		{
			offset = LWPtrParent(LWPathBuf(src), LWPathLenGet(src), -1);
			offset++;
		}
		if (offset && src == pathName)
			err = LWPathNCat(&tmp, 0, LWPathBuf(src), offset);
		if (statbuf.st_size)
			err = LWPathNCat(&tmp, offset, LWPathBuf(*target), statbuf.st_size);
			
		/* lstat could fail as the symlink path does not have to point to a valid file or directory */
		if (!err)
		{
			src = tmp;
			spath = LWPathBuf(tmp);
			if (lstat(spath, &statbuf))
				break;
		}
	}

	if (!err && fileAttr)
		*fileAttr |= LVFileFlagsFromStat(&statbuf);
#endif
	LWPathDispose(&tmp);
	return err;
}

LibAPI(MgErr) LVFile_ReadLink(LWPathHandle *pathName, LWPathHandle *target, int32 resolveDepth, int32 *resolveCount, uInt32 *fileAttr)
{
	MgErr err = LWPathZeroTerminate(pathName, -1);
	if (!err)
		err = lvFile_ReadLink(*pathName, target, resolveDepth, resolveCount, fileAttr);
	return err;
}

static MgErr lvFile_DeleteFile(LWPathHandle pathName, LVBoolean ignoreReadOnly, LVBoolean symLink)
{
    MgErr err = noErr;
	int8 type = LWPathTypeGet(pathName); 
#if !Win32
	char *path = LWPathBuf(pathName);
#else
	Unused(symLink);
#endif

	if (type != fAbsPath && type != fUNCPath)
	{
		return mgArgErr;
	}

	if (ignoreReadOnly)
	{
#if Win32
		DWORD attrs = GetFileAttributesLW(pathName);
		if (attrs != INVALID_FILE_ATTRIBUTES && attrs & kWinFileInfoReadOnly)
		{
			if (!SetFileAttributesLW(pathName, attrs & ~kWinFileInfoReadOnly))
			{
				err = Win32GetLVFileErr();
			}
		}
	}
	if (!err)
	{
		if (!DeleteFileLW(pathName))
		{
			err = Win32GetLVFileErr();
		}
#else
		struct stat statbuf;
		if (!lstat(path, &statbuf) && (statbuf.st_mode & 0220 != 0220))
		{
			if (chmod(path, statbuf.st_mode | 0220) && errno != ENOTSUP)
			{
				err = UnixToLVFileErr();
			}
		}
	}
	if (!err)
	{
		if (!symLink)
		{
			int fd  = open(path, O_RDWR);  /* checks for write access to file */
			if (fd != -1)
			{
				close(fd);
			}
			else
			{
				err = fNoPerm;
			}
		}
		if (!err && unlink(path))	       /* checks for write access to parent of file */
		{
			err = UnixToLVFileErr();
		}
#endif
	}
	return err;
}

static MgErr lvFile_DeleteDir(LWPathHandle pathName, LVBoolean ignoreReadOnly)
{
	MgErr err = noErr;
	Unused(ignoreReadOnly);
#if Win32
	if (!RemoveDirectoryLW(pathName))
	{
		err = Win32GetLVFileErr();
	}
#else
	char *path = LWPathBuf(pathName);
	if (rmdir(path))
	{
		err = UnixToLVFileErr();
	}
#endif
	return err;
}

static MgErr lvFile_DeleteRecursive(LWPathHandle pathName, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 flags)
{
	int32 start = TypeArrItems(*typeArr);
	MgErr err = lvFile_ListDirectory(pathName, nameArr, typeArr, 0, 0);
	if (!err)
	{
		int32 index, end = TypeArrItems(*typeArr);
		if (end > start)
		{
			LStrHandle *namePtr = NameArrElms(*nameArr, start);
			FileTypePtr typePtr = TypeArrElms(*typeArr, start);
			int32 lenPath = LWPathLenGet(pathName);

			for (index = start; !err && index < end; index++, namePtr++, typePtr++)
			{
				err = LWPathAppendUStr(&pathName, lenPath, *namePtr);
				if (!err)
				{
					if (typePtr->flags & (kIsFile | kIsLink))
					{
						err = lvFile_DeleteFile(pathName, (LVBoolean)(flags & kDelIgnoreReadOnly), (LVBoolean)((typePtr->flags & kIsLink) == kIsLink));
					}
					else
					{
						if (!(flags & kFileOpNoRecursion))
						{
							err = lvFile_DeleteRecursive(pathName, nameArr, typeArr, flags);
							if (!err)
							{
								/* Reevaluate pointer as the handle could have been resized */
								namePtr = NameArrElms(*nameArr, index);
								typePtr = TypeArrElms(*typeArr, index);
							}
						}
					}
				}
			}
			LWPathLenSet(pathName, lenPath);
			LWPathCntDec(pathName);
		}
		if (!err)
		{
			err = lvFile_DeleteDir(pathName, (LVBoolean)(flags & kDelIgnoreReadOnly));
		}
	}
	ResizeHandles(nameArr, typeArr, start);
	return err;
}

LibAPI(MgErr) LVFile_Delete(LWPathHandle *pathName, uInt32 flags)
{
	LWPathHandle tempName = NULL;
	MgErr err = LWPathCopy(&tempName, *pathName);
	if (!err)
	{
		uInt32 fileFlags;
		err = lvFile_FileFlags(tempName, -1, &fileFlags);
		if (!err)
		{
			/* No intelligent way of even trying to support trash operation on these */
#if !Pharlap && !VXWorks
			if (flags & kDelMoveToTrash)
			{
				err = lvFile_MoveToTrash(tempName, flags);
			}
			else
#endif
			if (fileFlags & (kIsFile | kIsLink))
			{
				err = lvFile_DeleteFile(tempName, (LVBoolean)(flags & kDelIgnoreReadOnly), (LVBoolean)((fileFlags & kIsLink) == kIsLink));
			}
			else
			{
				LStrArrHdl nameArr = NULL;
				FileTypeArrHdl typeArr = NULL;

				err = lvFile_DeleteRecursive(tempName, &nameArr, &typeArr, (LVBoolean)(flags & kDelIgnoreReadOnly));

				ResizeHandles(&nameArr, &typeArr, -1);
			}
		}
		else if (err == fNotFound)
		{
			/* If the file or directory does not exist but the parent does and is a directory,
			   consider it all done without error */
			int32 offset = LWPtrParent(LWPathBuf(tempName), LWPathLenGet(tempName), -1);
			if (offset > 0)
			{
				err = lvFile_FileFlags(tempName, offset, &fileFlags);
				if (!err && fileFlags & kIsFile)
				{
					/* Parent can't be a file though */
					err = fNotFound;
				}
			}
		}
		LWPathDispose(&tempName);
	}
	return err;
}

static MgErr lvFile_CopyFile(LWPathHandle pathFrom, LWPathHandle pathTo, uInt32 flags)
{
	MgErr err = mgArgErr;
	int8 typeFrom = LWPathTypeGet(pathFrom); 
	int8 typeTo = LWPathTypeGet(pathTo); 

	if ((typeFrom == fAbsPath || typeFrom == fUNCPath) && (typeTo == fAbsPath || typeTo == fUNCPath))
	{	
		FileInfoRec fileInfoFrom, fileInfoTo;

		err = lvFile_FileInfo(pathFrom, FALSE, &fileInfoFrom);
		if (!err)
		{
#if Unix
			FileRefNum fromRefnum = kInvalidFileRefnum,
				       toRefnum = kInvalidFileRefnum;
			char databuf[8192];
#endif
			if ((flags & kFileOpReplaceMask) != kFileOpReplaceAlways)
			{
				err = lvFile_FileInfo(pathTo, FALSE, &fileInfoTo);
				if (!err)
				{
					switch (flags & kFileOpReplaceMask)
					{
						case kFileOpReplaceNever:
							return fDupPath;
						case kFileOpReplaceIfNewer:
							if (fileInfoFrom.mDate.u.f.val < fileInfoTo.mDate.u.f.val ||
								fileInfoFrom.mDate.u.f.val == fileInfoTo.mDate.u.f.val &&
								fileInfoFrom.mDate.u.f.fract <= fileInfoTo.mDate.u.f.fract)
							{
								return fDupPath;
							}
					}
					err = lvFile_DeleteFile(pathTo, TRUE, (LVBoolean)((fileInfoTo.fileType & kIsLink) == kIsLink));
				}
			}
#if Win32
			if (!CopyFileLW(pathFrom, pathTo, NULL, NULL, NULL, 0))
			{
				err = Win32GetLVFileErr();
			}
#elif MacOSX
			{
				copyfile_state_t state = copyfile_state_alloc();
				if (state)
				{
					copyfile_flags_t copyFlags = COPYFILE_DATA;
					if (flags & kFileOpMaintainAttrs)
						copyFlags |= COPYFILE_XATTR | COPYFILE_SECURITY | COPYFILE_SECURITY;
					if (copyfile(LWPathBuf(pathFrom), LWPathBuf(pathTo), &state, copyFlags) < 0)
					{
						err = UnixToLVFileErr();
					}
					copyfile_state_free(state);
				}
				else 
				{
					err = mFullErr;
				}
				return err;
			}
#else
			err = lvFile_OpenFile(&fromRefnum, pathFrom, kOpenFileRsrcData, openNormal, accessReadOnly, denyWriteOnly, 0);
			if (!err)
			{
				err = lvFile_OpenFile(&toRefnum, pathTo, kOpenFileRsrcData, openCreate, accessWriteOnly, denyReadWrite, fileInfoFrom.unixFlags & 0777);
				if (!err)
				{
					uInt32 count;
					do
					{
						err = lvFile_Read(fromRefnum, sizeof(databuf), &count, (UPtr)databuf);
						if (!err && count)
						{
							err = lvFile_Write(toRefnum, count, &count, (UPtr)databuf);
						}
					}
					while (!err);
					lvFile_CloseFile(toRefnum);
					if (err == fEOF)
						err = noErr;
				}
				lvFile_CloseFile(fromRefnum);
			}
#endif
			if (!err && flags & kFileOpMaintainAttrs)
			{
				err = lvFile_FileInfo(pathTo, TRUE, &fileInfoFrom);
			}
		}
	}
	return err;
}

static MgErr lvFile_CopyRecursive(LWPathHandle pathFrom, LWPathHandle pathTo, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 flags)
{
	int32 start = TypeArrItems(*typeArr);
	MgErr err = lvFile_ListDirectory(pathFrom, nameArr, typeArr, 0, 0);
	if (!err)
	{
		int32 lenTo = LWPathLenGet(pathTo);
		FileInfoRec fileInfo = {0};
		
		err = lvFile_FileInfo(pathFrom, FALSE, &fileInfo);
		if (!err)
		{
			err = lvFile_CreateDirectory(pathTo, lenTo, 0777);
			if (err == fDupPath)
			{
				uInt32 fileFlags;
				if (!lvFile_FileFlags(pathTo, lenTo, &fileFlags))
				{
					if (!(fileFlags & kIsFile))
					{
						err = noErr;
					}
				}
			}
		}
		if (!err)
		{
			int32 index, end = TypeArrItems(*typeArr);
			if (end > start)
			{
				LStrHandle *namePtr = NameArrElms(*nameArr, start);
				FileTypePtr typePtr = TypeArrElms(*typeArr, start);
				int32 lenFrom = LWPathLenGet(pathFrom);

				for (index = start; !err && index < end; index++, namePtr++, typePtr++)
				{
					err = LWPathAppendUStr(&pathFrom, lenFrom, *namePtr);
					if (!err)
					{
						err = LWPathAppendUStr(&pathTo, lenTo, *namePtr);
						if (!err)
						{
							if (typePtr->flags & kIsFile)
							{
								err = lvFile_CopyFile(pathFrom, pathTo, flags);
							}
							else
							{
								err = lvFile_CopyRecursive(pathFrom, pathTo, nameArr, typeArr, flags);
								if (!err)
								{
									/* Reevaluate pointer as the handle could have been resized */
									namePtr = NameArrElms(*nameArr, index);
									typePtr = TypeArrElms(*typeArr, index);
								}
							}
						}
					}
				}
				LWPathLenSet(pathFrom, lenFrom);
				LWPathCntDec(pathFrom);
				LWPathLenSet(pathTo, lenTo);
				LWPathCntDec(pathTo);
			}
			if (!err && flags & kFileOpMaintainAttrs)
			{
				lvFile_FileInfo(pathTo, TRUE, &fileInfo);
			}
		}
	}
	ResizeHandles(nameArr, typeArr, start);
	return err;
}

LibAPI(MgErr) LVFile_Copy(LWPathHandle *pathFrom, LWPathHandle *pathTo, uInt32 flags)
{
	LWPathHandle tempFrom = NULL, tempTo = NULL;
	MgErr err = LWPathCopy(&tempFrom, *pathFrom);
	if (!err)
	{
		err = err = LWPathCopy(&tempTo, *pathTo);
		if (!err)
		{
			uInt32 fileFlags;
			err = lvFile_FileFlags(tempFrom, -1, &fileFlags);
			if (!err)
			{
				if (fileFlags & kIsFile)
				{
					err = lvFile_CopyFile(tempFrom, tempTo, flags);
				}
				else
				{
					LStrArrHdl nameArr = NULL;
					FileTypeArrHdl typeArr = NULL;
				
					err = lvFile_CopyRecursive(tempFrom, tempTo, &nameArr, &typeArr, flags);
					
					ResizeHandles(&nameArr, &typeArr, -1);
				}
			}
			LWPathDispose(&tempTo);
		}
		LWPathDispose(&tempFrom);
	}
	return err;
}

static MgErr lvFile_MoveFile(LWPathHandle pathFrom, LWPathHandle pathTo, uInt32 flags)
{
	MgErr err = mgArgErr;
	int8 typeFrom = LWPathTypeGet(pathFrom); 
	int8 typeTo = LWPathTypeGet(pathTo); 
#if !Win32
	char *spath, *tpath;
#endif
	if ((typeFrom == fAbsPath || typeFrom == fUNCPath) && (typeTo == fAbsPath || typeTo == fUNCPath))
	{	
		FileInfoRec fileInfoFrom, fileInfoTo;

		err = lvFile_FileInfo(pathFrom, FALSE, &fileInfoFrom);
		if (!err)
		{
			if (!lvFile_FileInfo(pathTo, FALSE, &fileInfoTo))
			{
				switch (flags & kFileOpReplaceMask)
				{
					case kFileOpReplaceNever:
						return fDupPath;
					case kFileOpReplaceIfNewer:
						if (fileInfoFrom.mDate.u.f.val < fileInfoTo.mDate.u.f.val ||
							fileInfoFrom.mDate.u.f.val == fileInfoTo.mDate.u.f.val &&
							fileInfoFrom.mDate.u.f.fract <= fileInfoTo.mDate.u.f.fract)
						{
							return fDupPath;
						}
				}
				err = lvFile_DeleteFile(pathTo, LV_TRUE, (LVBoolean)((fileInfoTo.fileType & kIsLink) == kIsLink));
			}
		}

		if (!err)
		{
#if Win32
			if (!MoveFileLW(pathFrom, pathTo))
#else
			spath = LWPathBuf(pathFrom);
			tpath = LWPathBuf(pathTo);

			if (rename(spath, tpath))
#endif	
			{
				/* rename failed, try a real copy */
				err = lvFile_CopyFile(pathFrom, pathTo, flags);
				if (!err)
				{
					err = lvFile_DeleteFile(pathFrom, LV_TRUE, (LVBoolean)((fileInfoFrom.fileType & kIsLink) == kIsLink));
				}
			}

			if (!err & flags & kFileOpMaintainAttrs)
			{
				err = lvFile_FileInfo(pathTo, TRUE, &fileInfoFrom);
			}
		}
	}
	return err;
}

static MgErr lvFile_MoveRecursive(LWPathHandle pathFrom, LWPathHandle pathTo, LStrArrHdl *nameArr, FileTypeArrHdl *typeArr, uInt32 flags)
{
	int32 start = TypeArrItems(*typeArr);
	MgErr err = lvFile_ListDirectory(pathFrom, nameArr, typeArr, 0, 0);
	if (!err)
	{
		int32 lenTo = LWPathLenGet(pathTo);
		FileInfoRec fileInfo = {0};

		err = lvFile_FileInfo(pathFrom, FALSE, &fileInfo);
		if (!err)
		{
			err = lvFile_CreateDirectory(pathTo, lenTo, 0777);
			if (err == fDupPath)
			{
				uInt32 fileFlags;
				if (!lvFile_FileFlags(pathTo, lenTo, &fileFlags))
				{
					if (!(fileFlags & kIsFile))
					{
						err = noErr;
					}
				}
			}
		}
		if (!err)
		{
			int32 index, end = TypeArrItems(*typeArr);
			if (end > start)
			{
				LStrHandle *namePtr = NameArrElms(*nameArr, start);
				FileTypePtr typePtr = TypeArrElms(*typeArr, start);
				int32 lenFrom = LWPathLenGet(pathFrom);

				for (index = start; !err && index < end; index++, namePtr++, typePtr++)
				{
					err = LWPathAppendUStr(&pathFrom, lenFrom, *namePtr);
					if (!err)
					{
						err = LWPathAppendUStr(&pathTo, lenTo, *namePtr);
						if (!err)
						{
							if (TypeArrElms(*typeArr, index)->flags & kIsFile)
							{
								err = lvFile_MoveFile(pathFrom, pathTo, flags);
							}
							else
							{
								err = lvFile_MoveRecursive(pathFrom, pathTo, nameArr, typeArr, flags);
								if (!err)
								{
									/* Reevaluate pointer as the handle could have been resized */
									namePtr = NameArrElms(*nameArr, index);
									typePtr = TypeArrElms(*typeArr, index);
								}
							}
						}
					}
				}
				LWPathLenSet(pathFrom, lenFrom);
				LWPathCntDec(pathFrom);
				LWPathLenSet(pathTo, lenTo);
				LWPathCntDec(pathTo);
			}
			if (!err)
			{
				lvFile_FileInfo(pathTo, TRUE, &fileInfo);
				err = lvFile_DeleteDir(pathFrom, (LVBoolean)(flags & kDelIgnoreReadOnly));
			}
		}
	}
	ResizeHandles(nameArr, typeArr, start);
	return err;
}

LibAPI(MgErr) LVFile_Rename(LWPathHandle *pathFrom, LWPathHandle *pathTo, uInt32 flags)
{
	LWPathHandle tempFrom = NULL, tempTo = NULL;
	MgErr err = LWPathCopy(&tempFrom, *pathFrom);
	if (!err)
	{
		err = LWPathCopy(&tempTo, *pathTo);
		if (!err)
		{
			uInt32 fileFlags;
			err = lvFile_FileFlags(tempFrom, -1, &fileFlags);
			if (!err)
			{
				if (fileFlags & kIsFile)
				{
					err = lvFile_MoveFile(tempFrom, tempTo, flags);
				}
				else
				{
					LStrArrHdl nameArr = NULL;
					FileTypeArrHdl typeArr = NULL;
				
					err = lvFile_MoveRecursive(tempFrom, tempTo, &nameArr, &typeArr, flags);

					ResizeHandles(&nameArr, &typeArr, -1);
				}
			}
			LWPathDispose(&tempTo);
		}
		LWPathDispose(&tempFrom);
	}
	return err;
}

#if Win32 && !Pharlap
HRESULT BindToCsidl(int csidl, REFIID riid, void **ppv)
{
	PIDLIST_ABSOLUTE pidl;
	HRESULT hr = SHGetFolderLocation(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, &pidl);
	if (SUCCEEDED(hr))
	{
#if VISTA
		hr = SHBindToObject(NULL, pidl, NULL, riid, ppv);
#else
		IShellFolder *psfDesktop;
		hr = SHGetDesktopFolder(&psfDesktop);
		if (SUCCEEDED(hr))
		{
			if (pidl->mkid.cb)
			{
				hr = IShellFolder_BindToObject(psfDesktop, pidl, NULL, riid, ppv);
			}
			else
			{
				hr = IShellFolder_QueryInterface(psfDesktop, riid, ppv);
			}
			IShellFolder_Release(psfDesktop);
		}
#endif
		CoTaskMemFree(pidl);
	}
	return hr;
}
#endif

#if !Pharlap && !VxWorks
/* This borrows from the implementation from https://github.com/robertguetzkow/libtrashcan with some modifications to code flow.
   I'm not a proponent of goto labels at all, even if hidden behind a handle_error macro. I prefer a modest nesting of if
   statements, that makes the conditional code flow more obvious. */
static MgErr lvFile_MoveToTrash(LWPathHandle pathName, uInt32 flags)
{
    MgErr err = mgArgErr;
	int8 type = LWPathTypeGet(pathName); 

	if (type == fAbsPath || type == fUNCPath)
	{
#if Win32
		IFileOperation *pFO = NULL;
		IShellItem *pSI = NULL;
		HRESULT hr = CoCreateInstance(&CLSID_FileOperation, NULL, CLSCTX_ALL, &IID_IFileOperation, &pFO);
		if (SUCCEEDED(hr))
		{
			if (flags & kDelNoRecursion)
			{
				flags = FOF_NO_UI | FOFX_EARLYFAILURE | FOF_NORECURSION;
			}
			else
			{
				flags = FOF_NO_UI | FOFX_EARLYFAILURE;
			}

			if (IsWindows8OrGreater())
			{
				flags |= FOFX_ADDUNDORECORD | FOFX_RECYCLEONDELETE;
			}
			else
			{
				flags |= FOF_ALLOWUNDO;
			}

			hr = IFileOperation_SetOperationFlags(pFO, flags);
			if (SUCCEEDED(hr))
			{
				WCHAR *temp = LWPathBuf(pathName);
				int32 offset = HasNTFSDevicePrefix(temp, LWPathLenGet(pathName));
				
				hr = SHCreateItemFromParsingName(temp + offset, NULL, &IID_IShellItem, &pSI);
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
		if (SUCCEEDED(hr))
		{
			err = mgNoErr;
		}
		else
		{
#if DEBUG
			err = SHErrToLVFileErr(hr);
#else
			err = fIOErr;
#endif
		}
#elif  MacOSX
		err = fIOErr;

		Class NSAutoreleasePoolClass = objc_getClass("NSAutoreleasePool");
		SEL allocSel = sel_registerName("alloc");
		SEL initSel = sel_registerName("init");
		id poolAlloc = ((id(*)(Class, SEL))objc_msgSend)(NSAutoreleasePoolClass, allocSel);
		id pool = ((id(*)(id, SEL))objc_msgSend)(poolAlloc, initSel);

		// NSURL *nsurl = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file_to_trash]];
		Class NSStringClass = objc_getClass("NSString");
		SEL stringWithUTF8StringSel = sel_registerName("stringWithUTF8String:");
		id pathString = ((id(*)(Class, SEL, const char*))objc_msgSend)(NSStringClass, stringWithUTF8StringSel, LWPathBuf(pathName));

		Class NSURLClass = objc_getClass("NSURL");
		SEL fileURLWithPathSel = sel_registerName("fileURLWithPath:");
		id nsurl = ((id(*)(Class, SEL, id))objc_msgSend)(NSURLClass, fileURLWithPathSel, pathString);

		// NSFileManager *fileManager = [NSFileManager defaultManager];
		Class NSFileManagerClass = objc_getClass("NSFileManager");
		SEL defaultManagerSel = sel_registerName("defaultManager");
		id fileManager = ((id(*)(Class, SEL))objc_msgSend)(NSFileManagerClass, defaultManagerSel);

		// fileManager trashItemAtURL:nsurl resultingItemURL:&trashed error:&error]
		/* Only moves one item to the trash so we need to loop that in case of directory in recursive manner */
		SEL trashItemAtURLSel = sel_registerName("trashItemAtURL:resultingItemURL:error:");
		BOOL deleteSuccessful = ((BOOL(*)(id, SEL, id, id, id))objc_msgSend)(fileManager, trashItemAtURLSel, nsurl, nil, nil);
		if (deleteSuccessful)
		{
			err = mgNoErr;
		}

		SEL drainSel = sel_registerName("drain");
		((void(*)(id, SEL))objc_msgSend)(pool, drainSel);
#elif Unix
		/* Needs to be implemented but it is a pitta to do with no common standard how to do it right under Linux */
		err = mgNotSupported;
#else
		err = mgNotSupported;
#endif
	}
	return err;
}
#endif

static MgErr lvFile_CreateDirectory(LWPathHandle lwstr, int32 end, int16 permissions)
{
	MgErr err = mgNoErr;
	LWChar ch = 0;
#if !Win32
	char *path = SStrBuf(lwstr);
#endif

	if (end >= 0)
	{
		ch = LWPathBuf(lwstr)[end];
		LWPathBuf(lwstr)[end] = 0;
	}
#if Win32
	if (!CreateDirectoryLW(lwstr, NULL))
	{
		err = Win32GetLVFileErr();
	}
	else
	{
		DWORD attr = GetFileAttributesLW(lwstr);
		if (attr == INVALID_FILE_ATTRIBUTES)
		{
			err = Win32GetLVFileErr();
		}
		else
		{
			if (permissions & 0222)
			{
                /* clear read only bit */
                attr &= ~FILE_ATTRIBUTE_READONLY;
	        }
		    else
			{
                /* set read only bit */
                attr |= FILE_ATTRIBUTE_READONLY;
			}

			/* set new attribute */
			if (!SetFileAttributesLW(lwstr, attr))
			{
                /* error occured -- map error code and return */
				err = Win32GetLVFileErr();
			}
		}
	}
#else
#if VxWorks
	if (mkdir(path) ||
#else
	if (mkdir(path, (permissions & 0777)) ||
#endif
		chmod(path, (permissions & 0777))) /* ignore umask */
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
	 	int32 off, len = LWPathLenGet(pathName),
			  rootLen = LWPtrRootLen(ptr, len, -1, NULL);
		if (rootLen > 0 && LWPathCntGet(pathName))
		{
			uInt32 fileFlags = 0;

			/* Move up in path until we find a valid directory */
			for (off = len; off >= rootLen; off = LWPtrParent(ptr, off, rootLen)) 
			{
				err = lvFile_FileFlags(pathName, off, &fileFlags);
				if (err != fNotFound)
					break;
			}

			while (!err && off < len)
			{
				off = LWPtrNextElement(ptr, len, off);
				err = lvFile_CreateDirectory(pathName, off, permissions);
			}
		}
	}
	return err;
}

LibAPI(MgErr) LVFile_CreateDirectories(LWPathHandle *pathName, int16 permissions)
{
	LWPathHandle tempName = NULL;
	MgErr err = LWPathCopy(&tempName, *pathName);
	if (!err)
	{
		err = lvFile_CreateDirectories(tempName, permissions);
		LWPathDispose(&tempName);
	}
	return err;
}

MgErr lvFile_CloseFile(FileRefNum ioRefNum)
{
	MgErr err = mgNoErr;
	
	if (ioRefNum == kInvalidFileRefnum)
		return mgArgErr;

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

static MgErr lvFile_GetSize(FileRefNum ioRefNum, LVBoolean remainder, int64 *size)
{
    MgErr err = mgNoErr;
	FileOffset len, tell = { 0 };

	if (0 == ioRefNum)
		return mgArgErr;

	len.q = 0;
#if usesWinPath
	tell.l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell.l.hi, FILE_CURRENT);
	if (tell.l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	len.l.lo = SetFilePointer(ioRefNum, len.l.lo, (PLONG)&len.l.hi, FILE_END);
	if (len.l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	if (tell.q < len.q)
	{
		tell.l.lo = SetFilePointer(ioRefNum, tell.l.lo, (PLONG)&tell.l.hi, FILE_BEGIN);
		if (tell.l.lo == INVALID_SET_FILE_POINTER)
		{
			// INVALID_FILE_SIZE could be a valid value
			err = Win32GetLVFileErr();
			if (err)
				return err;
		}
	}
#else
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
	len.q = ftello64(ioRefNum);
	if (len.q == - 1)
	{
		return UnixToLVFileErr();
	}
	if (fseeko64(ioRefNum, tell.q, SEEK_SET) == -1)
	{
		return UnixToLVFileErr();
	}
#endif
	if (remainder)
	{
		*size = len.q - tell.q;
	}
	else
	{
		*size = len.q;
	}
	return err;
}

static MgErr lvFile_SetSize(FileRefNum ioRefNum, int64 size)
{
	MgErr err = mgNoErr;
	FileOffset len, tell = { 0 };

	if (0 == ioRefNum)
		return mgArgErr;
	if (size < 0)
		return mgArgErr;
	len.q = size;
#if usesWinPath
	tell.l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell.l.hi, FILE_CURRENT);
	if (tell.l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	len.l.lo = SetFilePointer(ioRefNum, len.l.lo, (PLONG)&len.l.hi, FILE_BEGIN);
	if (len.l.lo == INVALID_SET_FILE_POINTER)
	{
		// INVALID_FILE_SIZE could be a valid value
		err = Win32GetLVFileErr();
		if (err)
			return err;
	}
	if (SetEndOfFile(ioRefNum))
	{
		if (tell.q < size)
		{
			tell.l.lo = SetFilePointer(ioRefNum, tell.l.lo, (PLONG)&tell.l.hi, FILE_BEGIN);
			if (tell.l.lo == INVALID_SET_FILE_POINTER)
			{
				// INVALID_FILE_SIZE could be a valid value
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
	if (ftruncate64(fileno(ioRefNum), size) != 0)
	{
		return UnixToLVFileErr();
	}
	tell.q = ftello64(ioRefNum);
	if (tell.q == -1)
	{
		return UnixToLVFileErr();
	}
	if ((tell.q > size) && (fseeko64(ioRefNum, size, SEEK_SET) != 0))
	{
		return UnixToLVFileErr();
	}
#endif
	return err;
}

static MgErr lvFile_SetFilePos(FileRefNum ioRefNum, uInt32 mode, int64 offset)
{
	MgErr err = mgNoErr;
	FileOffset size, sought, tell = {0};

	if (0 == ioRefNum)
		return mgArgErr;

	if (!offset && (mode == fCurrent))
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
		sought.q = offset;
	}
	else if (mode == fCurrent)
	{
		tell.l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell.l.hi, FILE_CURRENT);
		if (tell.l.lo == INVALID_SET_FILE_POINTER)
		{	
		    // INVALID_FILE_SIZE could be a valid value
			err = Win32GetLVFileErr();
			if (err)
				return err;
		}
		sought.q = tell.q;
		sought.q += offset;
	}
	else /* fEnd */
	{
		sought.q = size.q;
		sought.q += offset;
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
		sought.q = tell.q;
		sought.q += offset;
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
		sought.q = offset;
	}
	else /* fEnd */
	{
		sought.q = size.q;
		sought.q += offset;
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

static MgErr lvFile_GetFilePos(FileRefNum ioRefNum, int64 *offset)
{
	FileOffset tell = {0};
	if (!ioRefNum || !offset)
		return mgArgErr;
#if usesWinPath
	tell.l.lo = SetFilePointer(ioRefNum, 0, (PLONG)&tell.l.hi, FILE_CURRENT);
	if (tell.l.lo == INVALID_SET_FILE_POINTER)
	{
		return Win32GetLVFileErr();
	}
#else
	errno = 0;
	tell.q = ftello64(ioRefNum);
	if (tell.q == -1)
	{
		return UnixToLVFileErr();
	}
#endif
	*offset = tell.q;
	return mgNoErr;
}

static MgErr lvFile_LockFile(FileRefNum ioRefNum, uInt32 mode, FileOffset *offset, FileOffset *length)
{
#if usesWinPath
	OVERLAPPED overlapped = {0};
	BOOL ret;
#else
	struct flock lock = {0};
#endif

	if (!ioRefNum)
		return mgArgErr;

#if usesWinPath
	overlapped.Offset = offset->l.lo;
	overlapped.OffsetHigh = offset->l.hi;
	if (length->q <= 0)
	{
		MgErr err = lvFile_GetSize(ioRefNum, LV_FALSE, &length->q);
		if (err)
			return err;
		length->q -= offset->q;
	}
	if (mode == denyNeither)
	{
		ret = UnlockFileEx(ioRefNum, 0, length->l.lo, length->l.hi, &overlapped);
	}
	else
	{
		ret = LockFileEx(ioRefNum, mode == denyReadWrite ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, length->l.lo, length->l.hi, &overlapped);
	}
	if (!ret)
	{
		return Win32GetLVFileErr();
	}
#else
	switch (mode)
	{
		case denyReadWrite:
			lock.l_type = F_WRLCK | F_RDLCK;
			break;
		case denyWriteOnly:
			lock.l_type = F_WRLCK;
			break;
		case denyNeither:
			lock.l_type = F_UNLCK;
			break;
	}
	lock.l_whence = SEEK_SET;
	lock.l_start = offset->q;
	lock.l_len = length->q;
	lock.l_pid = getpid();
	if (fcntl(fileno(ioRefNum), F_SETLK, &lock) == -1)
	{
		return UnixToLVFileErr();
	}
#endif
	return mgNoErr;
}

static MgErr lvFile_Read(FileRefNum ioRefNum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
{
    MgErr	err = mgNoErr;
#if usesWinPath
	DWORD actCount;
#else
	size_t actCount;
#endif

    if (!ioRefNum)
		return mgArgErr;
    if (outCount)
        *outCount = 0;

#if usesWinPath
	if (!ReadFile(ioRefNum, buffer, inCount, &actCount, NULL))
	{
		return Win32GetLVFileErr();
	}
#else
	errno = 0;
	actCount = fread(buffer, 1, inCount, ioRefNum);
	if (actCount != inCount)
	{
		if (actCount && feof(ioRefNum))
		{
			clearerr(ioRefNum);
			return noErr;
		}
		if (ferror(ioRefNum))
		{
			clearerr(ioRefNum);
			return UnixToLVFileErr();
		}
	}
#endif
    if (outCount)
        *outCount = (uInt32)actCount;
    return err;
}

static MgErr lvFile_Write(FileRefNum ioRefNum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
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
    actCount = fwrite(buffer, 1, inCount, ioRefNum);
    if (ferror(ioRefNum))
    {
        clearerr(ioRefNum);
        return UnixToLVFileErr();
    }
#endif
    if (outCount)
        *outCount = actCount;
    return err;
}

#if MacOSX
static char *namedResourceFork = "/..namedfork/rsrc";
#endif

 static MgErr lvFile_OpenFile(FileRefNum *ioRefNum, LWPathHandle lwstr, uInt32 rsrc, uInt32 openMode, uInt32 accessMode, uInt32 denyMode, uInt32 flags)
{
    MgErr err = noErr;
	uInt8 type = LWPathTypeGet(lwstr);
	int32 len = LWPathLenGet(lwstr);
#if usesPosixPath
    int fd, oFlags;
	char *theMode, *path;
#elif usesWinPath
    DWORD shareAcc, openAcc;
    DWORD createAcc = OPEN_EXISTING;
    int32 attempts = 3;
#if !Pharlap
	wchar_t *rsrcPostfix = NULL;
 #endif
#endif

	if (!len || !ioRefNum || (type != fAbsPath && type != fUNCPath))
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
		case openNormal:
			createAcc = OPEN_EXISTING;
			break;
		case openReplace:
			createAcc = TRUNCATE_EXISTING;
			break;
		case openCreate:
			createAcc = CREATE_NEW;
			break;
		case openOpenOrCreate:
			createAcc = OPEN_ALWAYS;
			break;
		case openReplaceOrCreate:
			createAcc = CREATE_ALWAYS;
			break;
	}

	switch (accessMode)
    {
		case accessReadWrite:
			openAcc = GENERIC_READ | GENERIC_WRITE;
			break;
		case accessReadOnly:
			openAcc = GENERIC_READ;
			break;
		case accessWriteOnly:
			openAcc = GENERIC_WRITE;
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
			shareAcc = FILE_SHARE_READ | FILE_SHARE_WRITE;
			break;
		default:
			return mgArgErr;
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
		HANDLE hFile = CreateFileLW(lwstr, openAcc, shareAcc, 0, createAcc, flags & kNoBuffering ? FILE_FLAG_NO_BUFFERING : FILE_ATTRIBUTE_NORMAL, 0);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error == ERROR_SHARING_VIOLATION && (--attempts > 0))
		    {
			    Sleep(50);
		    }
		    else
			{
				return Win32ToLVFileErr(error);
			}
		}
		else
		{
			*ioRefNum = hFile;
			break;
		}
	}

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

    switch (openMode)
    {
		case openNormal:
			oFlags = 0;
			break;
		case openReplace:
			oFlags = O_TRUNC;
			break;
		case openCreate:
			oFlags = O_CREAT | O_EXCL;
			break;
		case openOpenOrCreate:
			oFlags = O_CREAT;
			break;
		case openReplaceOrCreate:
			oFlags = O_CREAT | O_TRUNC;
			break;
		default:
			return mgArgErr;
	}

	switch (accessMode)
    {
		case accessReadWrite:
			if (oFlags == O_CREAT | O_TRUNC)
				theMode = "w+";
			else
				theMode = "r+";
			oFlags |= O_RDWR;

			break;
		case accessReadOnly:
			theMode = "r";
			oFlags |= O_RDONLY;
			break;
		case accessWriteOnly:
			theMode = "w";
			oFlags |= O_WRONLY;
			break;
		default:
			return mgArgErr;
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

	if (flags & kNoBuffering)
	{
		// TODO: Need to setup buffer to be page aligned
		oFlags |= O_DIRECT;
	}
 #if MacOSX
	if (rsrc == kOpenFileRsrcResource)
	{
		err = LWPathNCat(lwstr, -1, namedResourceFork, -1);
		if (err)
			return err;
	}
 #endif
 #if 0
	/* Test for file existence first to avoid creating file with mode "w". */
	if (openMode == openWriteOnlyTruncate)
	{
		uInt32 fileAttr;
		err = lvFile_FileAttr(lwstr, -1, &fileAttr);
		if (err || !(fileAttr & kIsFile))
			return fNotFound;
	}
 #endif
	errno = 0;
	path = LWPathBuf(lwstr);
	fd = open(path, oFlags, flags & 0666);
	if (fd != -1)
	{
 #ifdef HAVE_FCNTL
		/* Implement deny mode by range locking whole file */
		if (denyMode == denyReadWrite || denyMode == denyWriteOnly)
		{
			struct flock lockInfo;

			lockInfo.l_type = (accessMode == accessReadOnly) ? F_RDLCK : F_WRLCK;
			lockInfo.l_whence = SEEK_SET;	// Start at beginning of file
			lockInfo.l_start = 0;			// Offset from beginning of file
			lockInfo.l_len = 0;				// Lock entire file
			lockInfo.l_pid = getpid();		// Set process ID
			if (fcntl(fd, F_SETLK, FCNTL_PARAM3_CAST(&lockInfo)) == -1)
			{
				err = UnixToLVFileErr();
			}
		}
 #endif
		if (!err)
		{
			*ioRefNum = fdopen(fd, theMode);
			if (!*ioRefNum)
			{
				close(fd);
				return UnixToLVFileErr();
			}
		}
	}
	else
	{
		err = UnixToLVFileErr();
	}
#endif
	return err;
}

LibAPI(MgErr) LVFile_OpenFile(LVRefNum *refnum, LWPathHandle *pathName, uInt32 rsrc, uInt32 openMode, uInt32 accessMode, uInt32 denyMode, uInt32 flags)
{
	MgErr err = LWPathZeroTerminate(pathName, -1);
	if (!err)
	{
		FileRefNum ioRefNum = kInvalidFileRefnum;
		err = lvFile_OpenFile(&ioRefNum, *pathName, rsrc, openMode, accessMode, denyMode, flags);
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
	MgErr err = lvzlibDisposeRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_CloseFile(ioRefNum);
	}
	return err;
}

LibAPI(MgErr) LVFile_IsAFile(LVRefNum *refnum, LVBoolean *isAFile)
{
	*isAFile = (LVBoolean)(lvzlibGetRefnum(refnum, NULL, FileMagic) == noErr);
	return noErr;
}

LibAPI(MgErr) LVFile_GetSize(LVRefNum *refnum, LVBoolean remainder, FileOffset *size)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_GetSize(ioRefNum, remainder, &size->q);
	}
	return err;
}

LibAPI(MgErr) LVFile_SetSize(LVRefNum *refnum, FileOffset *size)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_SetSize(ioRefNum, size->q);
	}
	return err;
}

LibAPI(MgErr) LVFile_GetFilePos(LVRefNum *refnum, FileOffset *offset)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_GetFilePos(ioRefNum, &offset->q);
	}
	return err;
}

LibAPI(MgErr) LVFile_SetFilePos(LVRefNum *refnum, uInt32 mode, FileOffset *offset)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_SetFilePos(ioRefNum, mode, offset->q);
	}
	return err;
}

LibAPI(MgErr) LVFile_LockFile(LVRefNum *refnum, uInt32 mode, FileOffset *offset, FileOffset *length)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_LockFile(ioRefNum, mode, offset, length);
	}
	return err;
}

LibAPI(MgErr) LVFile_Read(LVRefNum *refnum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_Read(ioRefNum, inCount, outCount, buffer);
	}
	return err;
}

LibAPI(MgErr) LVFile_Write(LVRefNum *refnum, uInt32 inCount, uInt32 *outCount, UPtr buffer)
{
	FileRefNum ioRefNum;
	MgErr err = lvzlibGetRefnum(refnum, (voidp*)&ioRefNum, FileMagic);
	if (!err)
	{
		err = lvFile_Write(ioRefNum, inCount, outCount, buffer);
	}
	return err;
}

LibAPI(MgErr) LVError_ErrorMessage(int32 code, LStrHandle message)
{
	LStrArrHdl arr = (LStrArrHdl)DSNewHClr(sizeof(int32));
	MgErr err = CallChain(arr);
	if (!err)
	{

	}
	return err;
}