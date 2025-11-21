
/* sit.h: contains declarations for SIT headers */
/* These come from the StuffIt 1.5 documentation by Raymond Lau. */

typedef struct __attribute__((aligned(2), packed)) sitHdr {		/* 22 bytes */
	u_char	sig1[4];		/* = 'SIT!' -- for verification */
	u_char	numFiles[2];	/* number of files in archive */
	u_char	arcLen[4];		/* length of entire archive incl. */
	u_char	sig2[4];		/* = 'rLau' -- for verification */
	u_char	version;		/* version number */
	char reserved[7];
} sitHdr;

typedef struct __attribute__((aligned(2), packed)) fileHdr {	/* 112 bytes */
	u_char	compRMethod;		/* rsrc fork compression method */
	u_char	compDMethod;		/* data fork compression method */
	u_char	fName[64];			/* a STR63 */
	char	fType[4];			/* file type */
	char	fCreator[4];		/* creator... */
	char	FndrFlags[2];		/* copy of Finder flags */
	char	cDate[4];			/* creation date */
	char	mDate[4];			/* !restored-compat w/backup prgms */
	u_char	rLen[4];			/* decom rsrc length */
	u_char	dLen[4];			/* decomp data length */
	u_char	cRLen[4];			/* compressed lengths */
	u_char	cDLen[4];
	u_char	rsrcCRC[2];			/* crc of rsrc fork */
	u_char	dataCRC[2];			/* crc of data fork */
	char	reserved[6];
	u_char	hdrCRC[2];			/* crc of file header */
} fileHdr;

/* file format is:
	sitArchiveHdr
		file1Hdr
			file1RsrcFork
			file1DataFork
		file2Hdr
			file2RsrcFork
			file2DataFork
		.
		.
		.
		fileNHdr
			fileNRsrcFork
			fileNDataFork
*/

/* A new feature with v1.5 and later is support for HMFs.  They look like regular entries for compatibility…

	Hierarchy Maintained Folder entry format:

		startFolder header (folder1)
		.
		.	file1 in folder1
		.
		.	startFolder header for subfolder (sub1)
		.	.	files/folders in sub1 (folders will be bracketed by
		.	.				startFolder and endFolder)
		.	endFolder header (sub1)
		.
		.	Other files and/or folders in folder1
		.	.
		.	.
		.	.
		.	fileN/subN
		endFolder header (folder1)

		startFolder format:
			data compression method will be startFolder (32)
			rsrc compression method will be startFolder (32)
			If start of top most level folder
				Comp. Length will be entire compressed length of folder
					contents, embedded startFolders and
					all endFolders.  (top most startFolder excluded)
				Decompressed length will be entire decompressed size of
					folder contents
				fName contains folder name
				creationDate and modDate are valid
				Other fields may be invalid
			otherwise
				fName contains folder name
				All other fields invalid

		endFolder format:
			Data compression method will be endFolder (33)
			Other fields may be invalid


		To maintain compatibility, the header structure used is the same as
			fileHdr.  This may prove to be quite wasteful, but that’s life!
*/

/* compression methods */
#define noComp	0	/* just read each byte and write it to archive */
#define repComp 1	/* RLE compression */
#define lzwComp 2	/* LZW compression */
#define hufComp 3	/* Huffman compression */

#define encrypted 16	/* bit set if encrypted.  ex: encrypted+lpzComp */

#define startFolder 32	/* marks start of a new folder */
#define endFolder 33	/* marks end of the last folder "started" */

/* all other numbers are reserved */

/*
 * the format of a *.info file made by xbin
 */
typedef struct __attribute__((aligned(2), packed)) infoHdr {
	char	res0[2];
	char	name[64];	/*  2 (a str 63) */
	char	type[4];	/* 65 */
	char	creator[4];	/* 69 */
	char	flag[2];	/* 73 */
	char	res1[8];
	char	dlen[4];	/* 83 */
	char	rlen[4];	/* 87 */
	char	ctime[4];	/* 91 */
	char	mtime[4];	/* 95 */
} infoHdr;

/*
 * resource fork header
 */
typedef struct __attribute__((aligned(2), packed)) resHdr {
	char	resDataOffset[4];
	char	resMapOffset[4];
	char	resDataLength[4];
	char	resMapLength[4];
	char	reserved0[32];
	char	name[32];
	char	reserved1[2];
	char	type[4];
	char	creator[4];
	char	fdflags[2];
	char	reserved2[36];
} resHdr;
