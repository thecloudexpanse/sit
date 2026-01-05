/*
   sit - StuffIt archive utility for UNIX

   Creates a StuffIt 1.5.1-compatible archive from files or folders specified
   as arguments. The default output file is "archive.sit" if the -o option is
   not provided. Use -v, -vv, or -vvv to see increasingly verbose output.

   Files without a resource fork are assigned the default type TEXT and
   creator KAHL, identifying them as a text file created by THINK C.
   You can override the default type and creator with the -T and -C options.

   The -u option converts all linefeeds ('\n') to carriage returns ('\r').
   This is really only useful when archiving plain Unix text files which you
   intend to open in a classic Mac application like SimpleText or MacWrite.
   In general, you should avoid this option, especially if you are archiving
   other types of documents or applications.

   Examples:
     # create "archive.sit" containing three specified files
     sit file1 file2 file3
     # create "FolderArchive.sit" containing FolderToBeArchived
     sit -o FolderArchive.sit FolderToBeArchived

   This version by Ken McLeod, 15 Nov 2025.

   Adapted from code posted to comp.sources.mac on 6 Dec 1988 by Tom Bereiter,
   with the following header:

 * | sit - Stuffit for UNIX
 * | Puts unix data files into stuffit archive suitable for downloading
 * | to a Mac.  Automatically processes files output from xbin.
 * |
 * | Reverse engineered from unsit by Allan G. Weber, which was based on
 * | macput, which was based on ...
 * | Just like unsit this uses the host's version of compress to do the work.
 * |
 * | Examples:
 * | 1) take collection of UNIX text files and make them LSC text files
 * | when uncompressed on the mac:
 * |   sit -u -T TEXT -C KAHL file ...
 * | 2) Process output from xbin:
 * |   xbin file1	 (produces FileOne.{info,rsrc,data})
 * |   sit file1
 * |
 * | Tom Bereiter
 * | ..!{rutgers,ames}!cs.utexas.edu!halley!rolex!twb
 */
#define BSD

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#ifdef BSD
#include <sys/time.h>
#endif
#include "sit.h"
#include "appledouble.h"
#include "zopen.h"

/* Type compatibility for non-BSD systems */
#ifndef ushort
typedef unsigned short ushort;
#endif

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

/* Mac time of 00:00:00 GMT, Jan 1, 1970 */
#define TIMEDIFF 0x7c25b080

#define ENABLE_LZW_COMPRESSION 1

/* Platform compatibility macros */
#ifdef __APPLE__
#define HAVE_BIRTHTIME 1
#define HAVE_NAMEDFORK 1
#endif

/* Get timezone offset portably */
static long get_timezone_offset(void) {
#if defined(BSD) || defined(__GLIBC__)
    time_t now;
    struct tm *tm;
    time(&now);
    tm = localtime(&now);
    return tm->tm_gmtoff + (tm->tm_isdst ? 3600 : 0);
#else
    /* Fallback: assume UTC */
    return 0;
#endif
}

/* Safe write that checks for errors and partial writes */
static int safe_write(int fd, const void *buf, size_t count, const char *context) {
    ssize_t written = write(fd, buf, count);
    if (written < 0) {
        fprintf(stderr, "Error writing %s: %s\n", context, strerror(errno));
        return -1;
    }
    if ((size_t)written != count) {
        fprintf(stderr, "Partial write for %s: wrote %zd of %zu bytes\n",
                context, written, count);
        return -1;
    }
    return 0;
}

struct sitHdr sh;
struct fileHdr fh;

char buf[BUFSIZ]; /* BUFSIZ is defined in <stdio.h> as 1024 */
char *defoutfile = "archive.sit";
int ofd;
ushort crc;
int rmfiles;
int unixf;
int verbose;
char *Creator, *Type;

static void usage(char *arg0) {
    fprintf(stderr, "Usage: %s ", arg0);
    fprintf(stderr, "[-v] [-u] [-T type] [-C creator] [-o dstfile] file ...\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v           Verbose output (can specify more than once for extra info)\n");
    fprintf(stderr, "  -u           Convert '\\n' chars to '\\r' in file's data while archiving\n");
    fprintf(stderr, "  -T type      Use this four-character type code if file doesn't have one\n");
    fprintf(stderr, "  -C creator   Use this four-character creator if file doesn't have one\n");
    fprintf(stderr, "  -o dstfile   Create archive with this name (default is \"archive.sit\")\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  # create \"archive.sit\" containing three specified files\n");
    fprintf(stderr, "  %s file1 file2 file3\n", arg0);
    fprintf(stderr, "  # create \"FolderArchive.sit\" containing FolderToBeArchived\n");
    fprintf(stderr, "  %s -o FolderArchive.sit FolderToBeArchived\n", arg0);
    fprintf(stderr, "  # specify that untyped files are JPEG and open in GraphicConverter\n");
    fprintf(stderr, "  %s -o jpgArchive.sit -T JPEG -C GKON *.jpg\n", arg0);
}
extern char *optarg;
extern int optind;

/* function declarations */
extern ushort updcrc(ushort icrc, unsigned char *icp, int icnt);
off_t put_item(char *name, off_t *uncompressed);
off_t put_folder(char *name, off_t *uncompressed, int level);
off_t put_folder_entry(char *name, off_t startPos, off_t *unCmpLen, int mtype, int level);
off_t put_file(char *name, off_t *uncompressed, int level);
off_t dofork(char *name, int convert);
void cp2(uint16_t x, char *dest);
void cp4(uint32_t x, char *dest);
void convertFilesystemNameToMacRoman(char *fsName, char *macName, int maxLength);

int main(int argc, char **argv) {
	int i;
	off_t total=0, uncompressed=0, items=0;
	int c;

	if (argc < 2) {
		usage(argv[0]);
		exit(1);
	}
	while ((c=getopt(argc, argv, "o:uvC:T:h")) != EOF)
	switch (c) {
		case 'r':		/* REMOVED! 'r' option is too easily confused with 'recursive' */
			usage(argv[0]);
			exit(1);	/* %%% could prompt with "are you really sure? [y/N]" here */
			rmfiles++;	/* remove files when done */
			break;
		case 'o':		/* specify output file */
			defoutfile = optarg;
			break;
		case 'u':		/* unix file -- change '\n' to '\r' */
			unixf++;
			break;
		case 'v':		/* verbose output */
			verbose++;
			break;
		case 'C':		/* set Mac creator (as default for files without one) */
			Creator = optarg;
			break;
		case 'T':		/* set Mac file type (as default for files without one) */
			Type = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0]);
			exit(1);
	}

	if (verbose) {
		fprintf(stdout, "Creating archive file \"%s\"\n", defoutfile);
	}
	if ((ofd=creat(defoutfile,0644))<0) {
		perror(defoutfile);
		exit(1);
	}
	/* empty header, will seek back and fill in later */
	if (safe_write(ofd, &sh, sizeof(sh), "archive header") < 0) {
		exit(1);
	}
	if (verbose>2) {
		fprintf(stdout, "* archive header (%lld bytes)\n",
				(long long)sizeof(sh));
	}

	for (i=optind; i<argc; i++) {
		off_t n, len;
		n = put_item(argv[i],&len);
		if (n) {
			total += n;
			items += 1;
			uncompressed += len;
		}
	}
	total += sizeof(sh);
	uncompressed += sizeof(sh);

	/* header header */
	strncpy((char*)sh.sig1,"SIT!",4);
	cp2(items,(char*)sh.numFiles);
	cp4(total,(char*)sh.arcLen);
	strncpy((char*)sh.sig2,"rLau",4);
	sh.version = 1;

	lseek(ofd,0,0);
	if (safe_write(ofd, &sh, sizeof(sh), "final archive header") < 0) {
		exit(1);
	}
	if (close(ofd) < 0) {
		fprintf(stderr, "Error closing archive: %s\n", strerror(errno));
		exit(1);
	}
	if (verbose) {
		fprintf(stdout, "Wrote %lld bytes to \"%s\"\n",(long long)total,defoutfile);
		if (verbose>2) {
			fprintf(stdout, "Compressed: %lld bytes, Uncompressed: %lld bytes\n",
					(long long)total,(long long)uncompressed);
		}
		fprintf(stdout, "Savings: %lld%%\n",
				(long long)100-((total*100)/uncompressed));
	}
}

off_t put_item(char *name, off_t *uncompressed) {
	struct stat st;
	off_t n = 0; /* total compressed bytes of item */
	*uncompressed = 0; /* total uncompressed bytes of item */

	if (lstat(name,&st)==0 && S_ISDIR(st.st_mode)) {
		/* this is a directory. */
		off_t startPos = lseek(ofd,0,1); /* remember where we are */
		if (verbose>1) { fprintf(stdout, "+ %s (directory)\n", basename(name)); }
		n += put_folder_entry(name,startPos,uncompressed,startFolder,0);
		n += put_folder(name,uncompressed,1);
		n += put_folder_entry(name,startPos,uncompressed,endFolder,0);
	}
	else {
		if (verbose>1) { fprintf(stdout, "+ %s\n", name); }
		n += put_file(name,uncompressed,0);
	}
	return n;
}

off_t put_folder(char *name, off_t *uncompressedLen, int level) {
	DIR *dir;
	struct dirent *entry;
	char path[PATH_MAX];
	int i, n = 0;

	if (!(dir = opendir(name))) {
		return 0;
	}
	while ((entry = readdir(dir)) != NULL) {
		struct stat entry_st;
		off_t uncompressedEntryLen = 0;

		/* Skip . and .. */
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (snprintf(path, sizeof(path), "%s/%s", name, entry->d_name) >= sizeof(path)) {
			fprintf(stderr, "Warning: path too long, skipping: %s/%s\n", name, entry->d_name);
			continue;
		}
		/* Use lstat to check if it's a directory (portable) */
		if (lstat(path, &entry_st) != 0) {
			perror(path);
			continue;
		}
		if (S_ISDIR(entry_st.st_mode)) { /* if it's a directory */
			off_t startPos = lseek(ofd,0,1); /* remember where we are */
			if (verbose>1) {
				for (i=0;i<level;i++) { fprintf(stdout, "  "); }
				fprintf(stdout, "+ %s (directory)\n", entry->d_name);
			}
			n += put_folder_entry(path,startPos,&uncompressedEntryLen,startFolder,level);
			n += put_folder(path,&uncompressedEntryLen,level+1); /* recursion! */
			n += put_folder_entry(path,startPos,&uncompressedEntryLen,endFolder,level);
		} else {
			if (strcmp(entry->d_name, ".DS_Store") == 0) { /* skip .DS_Store files */
				if (verbose>1) {
					for (i=0;i<level;i++) { fprintf(stdout, "  "); }
					fprintf(stdout, "! %s (skipped)\n", entry->d_name);
				}
				continue;
			}
			if (verbose>1) {
				for (i=0;i<level;i++) { fprintf(stdout, "  "); }
				fprintf(stdout, "+ %s\n", entry->d_name);
			}
			n += put_file(path,&uncompressedEntryLen,level);
		}
		*uncompressedLen += uncompressedEntryLen;
	}
	closedir(dir);
	return n;
}

/* put_folder_entry returns the compressed length of this entry's contents.
 * The uncompressed length is added to the value in the output argument.
 */
off_t put_folder_entry(char *name, off_t startPos, off_t *uncompressedLen, int mtype, int level) {
	/* This function adds the special startFolder and endFolder entries
	   that StuffIt uses to bracket the contents of a directory.

	   When mtype is startFolder, the supplied startPos is our current
	   position, where we are about to write the startFolder entry. The
	   return and output values will simply be the constant length of the
	   startFolder header.

	   When mtype is endFolder, startPos is the beginning of the matching
	   startFolder entry. The difference between the end of that startFolder
	   entry and the end of the endFolder entry gives us the total compressed
	   bytes that we added with this folder. (We are supposed to exclude the
	   startFolder entry itself, but include the endFolder.) Note that we
	   actually use the beginning of the startFolder and the beginning of the
	   endFolder, because we already have those offsets and their difference
	   is the same number of bytes due to the headers being the same length.

	   The compressed total is then written to the startFolder entry's cDLen
	   field. The uncompressed total is written to the dLen field.
	 */
	struct stat st;
	int i;
	long fpos1, fpos2;
	char *fname;
	long tdiff;
	struct tm *tp;
	time_t ctime, mtime;
	long bs;

	fpos1 = lseek(ofd,0,1); /* remember where we are (beginning of header) */
	if (fpos1 < 0) {
		fprintf(stderr, "Error seeking in archive: %s\n", strerror(errno));
		return 0;
	}
	/* write empty header, will seek back and fill in later */
	memset(&fh, 0, sizeof(fh));
	if (safe_write(ofd, &fh, sizeof(fh), "folder header") < 0) {
		return 0;
	}

	if (!(stat(name,&st)==0)) { /* get folder times */
		perror(name);
		return 0;
	}
	fname = basename(name);
	if (!fname) fname = name;
	if (verbose>2) {
		for (i=0;i<level;i++) { fprintf(stdout, "  "); }
		fprintf(stdout, "* %sFolder for %s (%lld bytes)\n",
				(mtype==startFolder) ? "start" : "end", fname,
				(long long)sizeof(fh));
	}
	convertFilesystemNameToMacRoman(fname,(char*)&fh.fName[0],63);
	ctime = st.st_ctime; /* ctime is really "time of last inode status change" */
#ifdef HAVE_BIRTHTIME
	ctime = st.st_birthtime; /* actual creation time is found in "birthtime" */
#endif
	mtime = st.st_mtime;
	/* convert unix file time to mac time format */
	tdiff = TIMEDIFF + get_timezone_offset();
	cp4(ctime + tdiff,(char*)fh.cDate);
	cp4(mtime + tdiff,(char*)fh.mDate);
	fh.compRMethod = fh.compDMethod = mtype;
	cp4(*uncompressedLen,(char*)fh.dLen);
	cp4(fpos1-startPos,(char*)fh.cDLen); /* 0 if startFolder */

	if (verbose>2 /*&& mtype==endFolder*/) {
		for (i=0;i<level;i++) { fprintf(stdout, "  "); }
		fprintf(stdout, "* compressed:%lld, uncompressed:%lld\n",
				(long long)(fpos1-startPos),(long long)*uncompressedLen);
	}
	crc = updcrc(0,(unsigned char*)&fh,(sizeof(fh))-2);
	cp2(crc,(char*)fh.hdrCRC);

	fpos2 = lseek(ofd,0,SEEK_CUR);		/* remember where we are (end of header) */
	if (fpos2 < 0 || lseek(ofd,fpos1,SEEK_SET) < 0) {
		fprintf(stderr, "Error seeking in archive: %s\n", strerror(errno));
		return 0;
	}
	if (safe_write(ofd, &fh, sizeof(fh), "folder header update") < 0) {
		return 0;
	}
	if (mtype==endFolder) {
		fh.compRMethod = fh.compDMethod = startFolder; /* fix up startFolder entry */
		crc = updcrc(0,(unsigned char*)&fh,(sizeof(fh))-2);
		cp2(crc,(char*)fh.hdrCRC);
		if (lseek(ofd,startPos,SEEK_SET) < 0) {
			fprintf(stderr, "Error seeking in archive: %s\n", strerror(errno));
			return 0;
		}
		if (safe_write(ofd, &fh, sizeof(fh), "startFolder header update") < 0) {
			return 0;
		}
	}
	if (lseek(ofd,fpos2,SEEK_SET) < 0) {
		fprintf(stderr, "Error seeking in archive: %s\n", strerror(errno));
		return 0;
	}
	/* add header length to uncompressed total */
	*uncompressedLen += sizeof(fh);

	return (fpos2 - fpos1);
}

/* put_file returns the compressed length in the function result,
 * and uncompressed length in output argument.
 */
off_t put_file(char *name, off_t *uncompressedLen, int level) {
	struct stat st;
	struct infoHdr ih;
	struct resHdr rh;
	int i,n,fd;
	long fpos1, fpos2;
	char nbuf[PATH_MAX], *p;
	int fork=0;
	long tdiff;
	size_t rlen, dlen;
	size_t cRLen, cDLen;
	struct tm *tp;
	time_t ctime, mtime;
	long bs;

	fpos1 = lseek(ofd,0,SEEK_CUR); /* remember where we are */
	if (fpos1 < 0) {
		fprintf(stderr, "Error seeking in archive: %s\n", strerror(errno));
		return 0;
	}
	/* write empty header, will seek back and fill in later */
	memset(&fh, 0, sizeof(fh));
	if (safe_write(ofd, &fh, sizeof(fh), "file header") < 0) {
		return 0;
	}
	if (verbose>2) {
		for (i=0;i<level;i++) { fprintf(stdout, "  "); }
		fprintf(stdout, "* file header (%lld bytes)\n", (long long)sizeof(fh));
	}
	rlen = dlen = cRLen = cDLen = 0;

	/* look for resource fork - try multiple methods */

	/* Method 1: Try AppleDouble sidecar file */
	rlen = get_appledouble_rsrc_size(name);
	if (rlen > 0) {
		/* Write resource fork data and calculate CRC */
		cRLen = read_appledouble_rsrc_with_crc(name, ofd, &crc, updcrc);
		if (cRLen != rlen) {
			fprintf(stderr, "Warning: resource fork size mismatch for %s\n", name);
		}
		cp4(rlen, (char*)fh.rLen);
		cp4(cRLen, (char*)fh.cRLen);
		cp2(crc, (char*)fh.rsrcCRC);
		fh.compRMethod = (cRLen==rlen) ? noComp : lzwComp;
		fork++;
	}

	/* Method 2: Try legacy .rsrc file */
	if (rlen == 0) {
		if (snprintf(nbuf, sizeof(nbuf), "%s.rsrc", name) >= sizeof(nbuf)) {
			fprintf(stderr, "Error: path too long: %s.rsrc\n", name);
			return 0;
		}
		if (stat(nbuf,&st)==0 && st.st_size) {
			rlen = st.st_size;
			cRLen = dofork(nbuf,0);
			cp4(st.st_size,(char*)fh.rLen);
			cp4(cRLen,(char*)fh.cRLen);
			cp2(crc,(char*)fh.rsrcCRC);
			fh.compRMethod = (cRLen==rlen) ? noComp : lzwComp;
			fork++;
		}
		if (rmfiles) unlink(nbuf);	/* ignore errors */
	}

#ifdef HAVE_NAMEDFORK
	/* Method 3: Try macOS named fork */
	if (rlen == 0) {
		if (snprintf(nbuf, sizeof(nbuf), "%s/..namedfork/rsrc", name) >= sizeof(nbuf)) {
			fprintf(stderr, "Error: path too long: %s/..namedfork/rsrc\n", name);
			return 0;
		}
		if (stat(nbuf,&st)==0 && st.st_size) {
			rlen = st.st_size;
			cRLen = dofork(nbuf,0);
			cp4(st.st_size,(char*)fh.rLen);
			cp4(cRLen,(char*)fh.cRLen);
			cp2(crc,(char*)fh.rsrcCRC);
			fh.compRMethod = (cRLen==rlen) ? noComp : lzwComp;
			fork++;
		}
	}
#endif

	/* look for data fork */
	if (snprintf(nbuf, sizeof(nbuf), "%s", name) >= sizeof(nbuf)) {
		fprintf(stderr, "Error: path too long: %s\n", name);
		return 0;
	}
	if (stat(nbuf,&st)<0) {		/* first try plain name */
		if (snprintf(nbuf, sizeof(nbuf), "%s.data", name) >= sizeof(nbuf)) {
			fprintf(stderr, "Error: path too long: %s.data\n", name);
			return 0;
		}
		stat(nbuf,&st);
	}
	dlen = cDLen = st.st_size;
	if (st.st_size) {		/* data fork exists */
		cDLen = dofork(nbuf,unixf);
		cp4(st.st_size,(char*)fh.dLen);
		cp4(cDLen,(char*)fh.cDLen);
		cp2(crc,(char*)fh.dataCRC);
		fh.compDMethod = (cDLen==dlen) ? noComp : lzwComp;
		fork++;
	}
	if (fork == 0) {
		fprintf(stderr,"%s: no data or resource files\n",name);
		return 0;
	}
	if (rmfiles) unlink(nbuf);	/* ignore errors */

	/* look for .info file */
	if (snprintf(nbuf, sizeof(nbuf), "%s.info", name) >= sizeof(nbuf)) {
		fprintf(stderr, "Error: path too long: %s.info\n", name);
		return 0;
	}
	if ((fd=open(nbuf,O_RDONLY))>=0 && read(fd,&ih,sizeof(ih))==sizeof(ih)) {
		/* %%% should we verify that .info file is exactly sizeof(ih)? */
		strncpy((char*)fh.fName, ih.name,64);
		strncpy((char*)fh.fType, ih.type, 4);
		strncpy((char*)fh.fCreator, ih.creator, 4);
		strncpy((char*)fh.FndrFlags, ih.flag, 2);
		strncpy((char*)fh.cDate, ih.ctime, 4);
		strncpy((char*)fh.mDate, ih.mtime, 4);
	}
	else {	/* no info file, so try to use info from rsrc/AppleDouble, else fake it */
		char *fName = basename(name);
		AppleDoubleMetadata ad_meta;
		int have_metadata = 0;

		if (!fName) fName = name;
		convertFilesystemNameToMacRoman(fName,(char*)&fh.fName[0],63);
		/* default to LSC text file */
		strncpy((char*)fh.fType, Type ? Type : "TEXT", 4);
		strncpy((char*)fh.fCreator, Creator ? Creator : "KAHL", 4);

		/* Try AppleDouble metadata first */
		if (read_appledouble_metadata(name, &ad_meta) == 0) {
			strncpy((char*)fh.fType, ad_meta.type, 4);
			strncpy((char*)fh.fCreator, ad_meta.creator, 4);
			strncpy((char*)fh.FndrFlags, ad_meta.flags, 2);
			have_metadata = 1;
		}
#ifdef HAVE_NAMEDFORK
		/* Try reading from macOS named fork if AppleDouble didn't work */
		if (!have_metadata) {
			if (snprintf(nbuf, sizeof(nbuf), "%s/..namedfork/rsrc", name) >= sizeof(nbuf)) {
				fprintf(stderr, "Warning: path too long for resource fork metadata: %s\n", name);
			} else if (rlen && (fd=open(nbuf,O_RDONLY))>=0 && read(fd,&rh,sizeof(rh))==sizeof(rh)) {
				strncpy((char*)fh.fType, rh.type, 4);
				strncpy((char*)fh.fCreator, rh.creator, 4);
				strncpy((char*)fh.FndrFlags, rh.fdflags, 2);
				have_metadata = 1;
			}
		}
#endif
		ctime = st.st_ctime; /* ctime is really "time of last inode status change" */
#ifdef HAVE_BIRTHTIME
		ctime = st.st_birthtime; /* actual creation time is found in "birthtime" */
#endif
		mtime = st.st_mtime;
		/* convert unix file time to mac time format */
		tdiff = TIMEDIFF + get_timezone_offset();
		cp4(ctime + tdiff, (char*)fh.cDate);
		cp4(mtime + tdiff, (char*)fh.mDate);
	}
	if (fd >= 0) close(fd);
	if (snprintf(nbuf, sizeof(nbuf), "%s.info", name) < sizeof(nbuf)) {
		if (rmfiles) unlink(nbuf);	/* ignore errors */
	}
	if (verbose) {
		char typecreator[10];
		snprintf(typecreator, sizeof(typecreator), "%.4s/%.4s", fh.fType, fh.fCreator);
		if (verbose>1) { for (i=0;i<level;i++) { fprintf(stdout, "  "); } }
		fprintf(stdout, "%s (%lld bytes) Data:%lld Rsrc:%lld [%s]\n",
				name,(long long)dlen+rlen,(long long)dlen,(long long)rlen,typecreator);
		if (verbose>2) {
			for (i=0;i<level;i++) { fprintf(stdout, "  "); }
			fprintf(stdout, "Savings: %lld%% (%lld/%lld bytes) Data:%lld/%lld Rsrc:%lld/%lld\n",
					(long long)100-(((cDLen+cRLen)*100)/(dlen+rlen)),
					(long long)cDLen+cRLen,(long long)dlen+rlen,
					(long long)cDLen,(long long)dlen,
					(long long)cRLen,(long long)rlen);
		}
	}
	crc = updcrc(0,(unsigned char*)&fh,(sizeof fh)-2);
	cp2(crc,(char*)fh.hdrCRC);

	fpos2 = lseek(ofd,0,SEEK_CUR);		/* remember where we are */
	if (fpos2 < 0 || lseek(ofd,fpos1,SEEK_SET) < 0) {
		fprintf(stderr, "Error seeking in archive: %s\n", strerror(errno));
		return 0;
	}
	if (safe_write(ofd, &fh, sizeof fh, "file header update") < 0) {
		return 0;
	}
	if (lseek(ofd,fpos2,SEEK_SET) < 0) {
		fprintf(stderr, "Error seeking in archive: %s\n", strerror(errno));
		return 0;
	}
	*uncompressedLen += rlen + dlen + sizeof(fh);

	return (fpos2 - fpos1);
}

/* Processes contents of given file, writing compressed data
 * to the output archive and returning the compressed length.
 */
off_t dofork(char *name, int convert) {
	FILE *fs, *cfs;
	int fd, ufd;
	size_t n, clen;
	char *p;
	char cvtfilename[] = "/tmp/sit+cvt-XXXXXX";
	char cmpfilename[] = "/tmp/sit+cmp-XXXXXX";

	if ((fd=mkstemp(cmpfilename))<0) {
		perror(cmpfilename);
		return 0;
	}
	close(fd);

	if ((fd=open(name,O_RDONLY))<0) {
		perror(name);
		return 0;
	}
	if (convert) {	/* build conversion file */
		if ((ufd=mkstemp(cvtfilename))<0) {
			perror(cvtfilename);
			close(fd);
			return 0;
		}
	}
	/* do crc of file: */
	crc = 0;
	while ((n=read(fd,buf,BUFSIZ))>0) {
		if (convert) {	/* convert '\n' to '\r' */
			for (p=buf; p<&buf[n]; p++)
				if (*p == '\n') *p = '\r';
			if (safe_write(ufd, buf, n, "conversion temp file") < 0) {
				close(fd);
				if (convert) close(ufd);
				return 0;
			}
		}
		crc = updcrc(crc,(unsigned char*)buf,n);
	}
	close(fd);
	if (convert) { close(ufd); }

#if ENABLE_LZW_COMPRESSION
	/* open file stream for compressed output */
	if ((cfs=zopen(cmpfilename,"w",14))==NULL) { /* always 14 bits */
#else
	/* open file stream for uncompressed output */
	if ((cfs=fopen(cmpfilename,"w"))==NULL) {
#endif
		perror(cmpfilename);
		return 0;
	}
	if (convert) { /* use conversion file as input */
		if ((fs=fopen(cvtfilename,"r"))==NULL) {
			perror(cvtfilename);
			return 0;
		}
	}
	else { /* use original file as input */
		if ((fs=fopen(name,"r"))==NULL) {
			perror(name);
			return 0;
		}
	}
	/* write compressed data to temp file */
	while ((n=fread(buf,1,BUFSIZ,fs))>0) {
		if (fwrite(buf, 1, n, cfs) != n) {
			perror("fork data");
			fclose(fs);
			fclose(cfs);
			return 0;
		}
	}
	fclose(fs);
	fclose(cfs);
	unlink(cvtfilename); /* ignore error */

	/* reopen temp file */
	if ((fd=open(cmpfilename,O_RDONLY))<0) {
		perror(cmpfilename);
		return 0;
	}
	/* write temp file to output archive */
	clen = 0;
#if ENABLE_LZW_COMPRESSION
	/* skip past initial 3-byte compress header (1f 9d 8e) */
	if (lseek(fd,3L,SEEK_SET) < 0) {
		fprintf(stderr, "Error seeking in compressed data: %s\n", strerror(errno));
		close(fd);
		return 0;
	}
#endif
	while ((n=read(fd,buf,BUFSIZ))>0) {
		if (safe_write(ofd, buf, n, "fork data") < 0) {
			close(fd);
			return 0;
		}
		clen += n;
	}
	close(fd);
	unlink(cmpfilename); /* ignore error */
	return clen;
}

void cp2(uint16_t x, char *dest) {
	dest[0] = x>>8;
	dest[1] = x;
}

void cp4(uint32_t x, char *dest) {
	dest[0] = x>>24;
	dest[1] = x>>16;
	dest[2] = x>>8;
	dest[3] = x;
}

/* each entry is 4 bytes: first 2 (or 3) are UTF-8, 4th byte is matching Mac char.
 * obviously this could be made more space-efficient. */
static u_char hiBitCharMap[] = {
	0xC3,0x84,0x00,0x80,0xC3,0x85,0x00,0x81,0xC3,0x87,0x00,0x82,0xC3,0x89,0x00,0x83,
	0xC3,0x91,0x00,0x84,0xC3,0x96,0x00,0x85,0xC3,0x9C,0x00,0x86,0xC3,0xA1,0x00,0x87,
	0xC3,0xA0,0x00,0x88,0xC3,0xA2,0x00,0x89,0xC3,0xA4,0x00,0x8A,0xC3,0xA3,0x00,0x8B,
	0xC3,0xA5,0x00,0x8C,0xC3,0xA7,0x00,0x8D,0xC3,0xA9,0x00,0x8E,0xC3,0xA8,0x00,0x8F,
	0xC3,0xAA,0x00,0x90,0xC3,0xAB,0x00,0x91,0xC3,0xAD,0x00,0x92,0xC3,0xAC,0x00,0x93,
	0xC3,0xAE,0x00,0x94,0xC3,0xAF,0x00,0x95,0xC3,0xB1,0x00,0x96,0xC3,0xB3,0x00,0x97,
	0xC3,0xB2,0x00,0x98,0xC3,0xB4,0x00,0x99,0xC3,0xB6,0x00,0x9A,0xC3,0xB5,0x00,0x9B,
	0xC3,0xBA,0x00,0x9C,0xC3,0xB9,0x00,0x9D,0xC3,0xBB,0x00,0x9E,0xC3,0xBC,0x00,0x9F,
	0xE2,0x80,0xA0,0xA0,0xC2,0xB0,0x00,0xA1,0xC2,0xA2,0x00,0xA2,0xC2,0xA3,0x00,0xA3,
	0xC2,0xA7,0x00,0xA4,0xE2,0x80,0xA2,0xA5,0xC2,0xB6,0x00,0xA6,0xC3,0x9F,0x00,0xA7,
	0xC2,0xAE,0x00,0xA8,0xC2,0xA9,0x00,0xA9,0xE2,0x84,0xA2,0xAA,0xC2,0xB4,0x00,0xAB,
	0xC2,0xA8,0x00,0xAC,0xE2,0x89,0xA0,0xAD,0xC3,0x86,0x00,0xAE,0xC3,0x98,0x00,0xAF,
	0xE2,0x88,0x9E,0xB0,0xC2,0xB1,0x00,0xB1,0xE2,0x89,0xA4,0xB2,0xE2,0x89,0xA5,0xB3,
	0xC2,0xA5,0x00,0xB4,0xC2,0xB5,0x00,0xB5,0xE2,0x88,0x82,0xB6,0xE2,0x88,0x91,0xB7,
	0xE2,0x88,0x8F,0xB8,0xCF,0x80,0x00,0xB9,0xE2,0x88,0xAB,0xBA,0xC2,0xAA,0x00,0xBB,
	0xC2,0xBA,0x00,0xBC,0xCE,0xA9,0x00,0xBD,0xC3,0xA6,0x00,0xBE,0xC3,0xB8,0x00,0xBF,
	0xC2,0xBF,0x00,0xC0,0xC2,0xA1,0x00,0xC1,0xC2,0xAC,0x00,0xC2,0xE2,0x88,0x9A,0xC3,
	0xC6,0x92,0x00,0xC4,0xE2,0x89,0x88,0xC5,0xE2,0x88,0x86,0xC6,0xC2,0xAB,0x00,0xC7,
	0xC2,0xBB,0x00,0xC8,0xE2,0x80,0xA6,0xC9,0xC2,0xA0,0x00,0xCA,0xC3,0x80,0x00,0xCB,
	0xC3,0x83,0x00,0xCC,0xC3,0x95,0x00,0xCD,0xC5,0x92,0x00,0xCE,0xC5,0x93,0x00,0xCF,
	0xE2,0x80,0x93,0xD0,0xE2,0x80,0x94,0xD1,0xE2,0x80,0x9C,0xD2,0xE2,0x80,0x9D,0xD3,
	0xE2,0x80,0x98,0xD4,0xE2,0x80,0x99,0xD5,0xC3,0xB7,0x00,0xD6,0xE2,0x97,0x8A,0xD7,
	0xC3,0xBF,0x00,0xD8
};

/* Try to convert filesystem name (assumes UTF-8) to a MacRoman string for StuffIt.
 * Currently, this just means converting the extended ascii characters.
 * Also convert colon characters to slashes, since they are path delimiters and
 * the item will not be extractable with StuffIt Expander unless we do.
 * Input is a C string, in UTF-8 encoding.
 * Output is a P string, in MacRoman encoding if possible.
 * Note: this doesn't use CFString, since the characters we can convert are known
 * and can be mapped to their MacRoman equivalent, and we want to remain portable.
 */
void convertFilesystemNameToMacRoman(char *fsName, char *macName, int maxLength) {
	u_char tmp[maxLength+1];
	u_char *p = (u_char*)fsName;
	int i, remaining = min(strlen((char*)fsName),maxLength);
	tmp[0] = 0;
	while (remaining > 0) {
		u_char c = *p;
		if (c >= 0x80 && remaining > 1) {
			for (i=0; i<sizeof(hiBitCharMap); i=i+4) {
				if (c == hiBitCharMap[i]) {
					if (*(p+1) == hiBitCharMap[i+1]) {
						if (0x00 == hiBitCharMap[i+2]) {
							/* matched 2-byte UTF-8 character */
							c = hiBitCharMap[i+3];
							p = p+1;
							remaining -= 1;
							break;
						} else if (remaining > 2 && *(p+2) == hiBitCharMap[i+2]) {
							/* matched 3-byte UTF-8 character */
							c = hiBitCharMap[i+3];
							p = p+2;
							remaining -= 2;
							break;
						}
					}
				}
			}
		} else {
			if (c == 0x3A) { c = 0x2F; } /* convert colon to slash */
		}
		tmp[tmp[0]+1] = c;
		tmp[0] = tmp[0]+1;
		p = p+1;
		remaining -= 1;
	}
	memcpy(macName,tmp,tmp[0]+1);
}
