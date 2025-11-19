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

   Unlike the original StuffIt, this one does not currently compress data.
   It just provides a way to create a container that can be safely transferred
   from a modern system to a classic Mac computer or emulator, where it can
   be opened with StuffIt or StuffIt Expander.

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

/* Type compatibility for non-BSD systems */
#ifndef ushort
typedef unsigned short ushort;
#endif

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

/* Mac time of 00:00:00 GMT, Jan 1, 1970 */
#define TIMEDIFF 0x7c25b080

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
size_t clen;
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
int put_item(char *name);
int put_folder(char *name, int level);
int put_folder_entry(char *name, long startPos, int mtype, int level);
int put_file(char *name, int level);
int dofork(char *name, int convert);
void cp2(uint16_t x, char *dest);
void cp4(uint32_t x, char *dest);

int main(int argc, char **argv) {
	int i,n;
	int total=0, items=0;
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
	if (safe_write(ofd, &sh, sizeof sh, "archive header") < 0) {
		exit(1);
	}

	for (i=optind; i<argc; i++) {
		n = put_item(argv[i]);
		if (n) {
			total += n;
			items += 1;
		}
	}
	lseek(ofd,0,0);

	total += sizeof(sh);
	/* header header */
	strncpy((char*)sh.sig1,"SIT!",4);
	cp2(items,(char*)sh.numFiles);
	cp4(total,(char*)sh.arcLen);
	strncpy((char*)sh.sig2,"rLau",4);
	sh.version = 1;

	if (safe_write(ofd, &sh, sizeof sh, "final archive header") < 0) {
		exit(1);
	}
	if (close(ofd) < 0) {
		fprintf(stderr, "Error closing archive: %s\n", strerror(errno));
		exit(1);
	}
	if (verbose) {
		fprintf(stdout, "Wrote %d bytes to \"%s\"\n",total,defoutfile);
	}
}

int put_item(char *name) {
	int n = 0;
	struct stat st;
	if (lstat(name,&st)==0 && S_ISDIR(st.st_mode)) {
		/* this is a directory. */
		long startPos = lseek(ofd,0,1); /* remember where we are */
		if (verbose>1) { fprintf(stdout, "+ %s (directory)\n", basename(name)); }
		n += put_folder_entry(name,startPos,startFolder,0);
		n += put_folder(name,1);
		n += put_folder_entry(name,startPos,endFolder,0);
	}
	else {
		if (verbose>1) { fprintf(stdout, "+ %s\n", name); }
		n += put_file(name,0);
	}
	return n;
}

int put_folder(char *name, int level) {
	DIR *dir;
	struct dirent *entry;
	char path[PATH_MAX];
	int i, n = 0;

	if (!(dir = opendir(name))) {
		return 0;
	}
	while ((entry = readdir(dir)) != NULL) {
		struct stat entry_st;

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
			long startPos = lseek(ofd,0,1); /* remember where we are */
			if (verbose>1) {
				for (i=0;i<level;i++) { fprintf(stdout, "  "); }
				fprintf(stdout, "+ %s (directory)\n", entry->d_name);
			}
			n += put_folder_entry(path,startPos,startFolder,level);
			n += put_folder(path,level+1); /* recursion! */
			n += put_folder_entry(path,startPos,endFolder,level);
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
			n += put_file(path,level);
		}
	}
	closedir(dir);
	return n;
}

int put_folder_entry(char *name, long startPos, int mtype, int level) {
	/* This function adds the special startFolder and endFolder entries that
	   StuffIt uses to bracket the contents of a directory.

	   When mtype is startFolder, the supplied startPos is our current position,
	   where we are about to write the startFolder entry.

	   When mtype is endFolder, startPos is the beginning of the matching
	   startFolder entry. The difference between the end of that startFolder entry
	   and the end of the endFolder entry gives us the total bytes that we added
	   with this folder. (We are supposed to exclude the startFolder entry itself,
	   but include the endFolder.) Note that we actually use the beginning of the
	   startFolder and the beginning of the endFolder, because we already have
	   those offsets and their difference is the same number of bytes due to the
	   folder entries being the same length.

	   That total is then written to the startFolder entry's cDLen and dlen fields,
	   which will both be the the same value if we aren't compressing anything.
	   When compression is reimplemented, we'll need to keep track of the
	   uncompressed data length separately and provide it to this function.
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
		fprintf(stdout, "* %sFolder for %s\n",
				(mtype==startFolder) ? "start" : "end", fname);
	}
	strncpy((char*)&fh.fName[1],fname,63); fh.fName[0] = min(strlen(fname),63);
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
	cp4(fpos1-startPos,(char*)fh.dLen);
	cp4(fpos1-startPos,(char*)fh.cDLen);

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

	return (fpos2 - fpos1);
}

int put_file(char *name, int level) {
	struct stat st;
	struct infoHdr ih;
	struct resHdr rh;
	int i,n,fd;
	long fpos1, fpos2;
	char nbuf[PATH_MAX], *p;
	int fork=0;
	long tdiff;
	size_t rlen, dlen;
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

	/* look for resource fork - try multiple methods */
	rlen = 0;

	/* Method 1: Try AppleDouble sidecar file */
	rlen = get_appledouble_rsrc_size(name);
	if (rlen > 0) {
		/* Write resource fork data and calculate CRC */
		clen = read_appledouble_rsrc_with_crc(name, ofd, &crc, updcrc);
		if (clen != rlen) {
			fprintf(stderr, "Warning: resource fork size mismatch for %s\n", name);
		}
		cp4(rlen, (char*)fh.rLen);
		cp4(clen, (char*)fh.cRLen);
		cp2(crc, (char*)fh.rsrcCRC);
		fh.compRMethod = noComp;
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
			dofork(nbuf,0);
			cp4(st.st_size,(char*)fh.rLen);
			cp4(clen,(char*)fh.cRLen);
			cp2(crc,(char*)fh.rsrcCRC);
			fh.compRMethod = noComp;
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
			dofork(nbuf,0);
			cp4(st.st_size,(char*)fh.rLen);
			cp4(clen,(char*)fh.cRLen);
			cp2(crc,(char*)fh.rsrcCRC);
			fh.compRMethod = noComp;
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
	dlen = st.st_size;
	if (st.st_size) {		/* data fork exists */
		dofork(nbuf,unixf);
		cp4(st.st_size,(char*)fh.dLen);
		cp4(clen,(char*)fh.cDLen);
		cp2(crc,(char*)fh.dataCRC);
		fh.compDMethod = noComp;
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
		strncpy((char*)&fh.fName[1],fName,63); fh.fName[0] = min(strlen(fName),63);
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

	return (fpos2 - fpos1);
}

int dofork(char *name, int convert) {
	int fd, ufd, tfd;
	size_t n;
	char *p;
	char *tempfilename = "sit+temp";

	if ((fd=open(name,O_RDONLY))<0) {
		perror(name);
		return 0;
	}
	if (convert) {	/* build conversion file */
		if ((ufd=creat(tempfilename,0644))<0) {
			perror(tempfilename);
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

	if (convert) { /* reopen temp file */
		close(ufd);
		if ((tfd=open(tempfilename,O_RDONLY))<0) {
			perror(tempfilename);
			return 0;
		}
	}
	else { /* reopen input file */
		if ((tfd=open(name,O_RDONLY))<0) {
			perror(name);
			return 0;
		}
	}
	/* write data to output archive */
	clen = 0;
	while ((n=read(tfd,buf,BUFSIZ))>0) {
		if (safe_write(ofd, buf, n, "fork data") < 0) {
			close(tfd);
			return 0;
		}
		clen += n;
	}
	close(tfd);
	unlink(tempfilename); /* ignore error */
	return 0;
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
