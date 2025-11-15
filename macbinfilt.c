/*
 * macbinfilt -- filters usenet articles from comp.binaries.mac into a form
 *  suitable for xbin to decode.  Will rearange parts of file if they are not
 *  in order.  Strips out all extraneous lines.
 *  Does all of this by making many bold assumtions--but it's worked so far.
 *
 *  Only works on one article at a time.  All files on the input line are
 *  considered parts of the same article.
 *
 *  If you have the sysV regualar expression routines (regcmp, regex) then
 *  define HAVE_REGCMP for a more robust pattern match.
 *
 *  --Tom Bereiter
 *    ..!{rutgers,ames}!cs.utexas.edu!halley!rolex!twb
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int cur_part,part,divert_part;
int max_part;
#define IBUFSZ	512
char ibuf[IBUFSZ];
char pname[80];
FILE *ofs;
FILE *saveofs;
FILE *parts[100];

#ifdef HAVE_REGCMP
#define EXPR ".*[Pp][Aa][Rr][Tt][ \t]*([0-9]+)$0[ \t]*[Oo][Ff][ \t]*([0-9]+)$1"
#else
#define EXPR "part %d of %d"
#endif
char *expr;

main(argc,argv) char **argv[]; {
	FILE *fs;
	int i,rc=0;

#ifdef HAVE_REGCMP
	expr = (char *)regcmp(EXPR,0);
#else
	expr = EXPR;
#endif
    ofs = stdout;
	fputs("(This file must be converted with BinHex 4.0)\n\n",ofs);

	if (argc == 1)
		filter(stdin);
	else while (--argc) {
		if ((fs=fopen((const char *)*(++argv),"r"))==NULL) {
			perror((const char*)*argv); exit(-1); }
		filter(fs);
		fclose(fs);
	}
	/* add any remaining parts */
	for (i=cur_part+1; i<=max_part; i++)
		if (parts[i])
			putpart(i);
		else {
			fprintf(stderr,"Missing part %d\n",i);
			rc = -1;
		}
	exit(rc);
}

/* valid xbin chars + '\n' and '\r' */
#define	Btst(i) (bmap[i>>3] & (1<<(i&07)))
char bmap[]={0x00,0x24,0x00,0x00,0xfe,0x3f,0x7f,0x07,
			 0xff,0x7f,0x7f,0x0f,0x7f,0x3f,0x07,0x00};

/* filter out extraneous lines and look for lines of the form:
 *    part n of m
 * A line is considered valid if it has only valid xbin characters and is
 * either greater than 60 characters or ends in a ':'
 */

filter(fs) FILE *fs; {
	register char *p,*inp;

reget:
	while ((inp=fgets(ibuf,IBUFSZ,fs))) {
		for (p=inp; *p; p++)
			if (Btst(*p) == 0) {	/* invalid character */
				checkparts(inp);
				goto reget;
			}
		if (p-inp > 60 || inp[(p-inp)-2]==':')	/* arbitrary max or end */
			fputs(ibuf,ofs);
	}
	if (divert_part)	/* diversion in progress */
		end_oseq();
}

checkparts(str) char *str; {
	char *p;
	char num0[40], num1[40];

#ifdef HAVE_REGEXP
	if (regex(expr, str, num0,num1)!=NULL) {
		part = atoi(num0);
		max_part = atoi(num1);
fprintf(stderr,"part %d of %d\n",part,max_part);
		dopart();
	}
#else
	for (p=str; *p; p++)	/* rescan for 'part' string */
		if (*p==expr[0])
			if (sscanf(p,expr,&part,&max_part) == 2) {
				dopart();
				break;
			}
#endif
}

dopart() {
	if (divert_part) {	/* diversion in progress */
		if (part == divert_part)	/* another mention of current part */
			return;
		end_oseq();
	}
	if (part == cur_part+1) 	/* OK: next in sequence */
		cur_part = part;
	else if (part > cur_part) 	/* out of sequence */
		oseq();
	else 	/* "can't" happen */
		fprintf(stderr,"Part %d unexpected\n",part);
}

/* part out of sequence */
oseq() {
	int i;

	/* try and fill in gap */
	for (i=cur_part+1; i<part; i++)
		if (parts[i]) {
			putpart(i);
			cur_part = i;
		}
		else goto isgap;
	/* all missing parts restored -- continue */
	return;
isgap:
	/* start diversion */
	divert_part = part;
	saveofs = ofs;
	sprintf(pname,"part%d",part);
	if ((ofs = fopen(pname,"w+")) == NULL) {
		perror((const char*)pname); exit(-1); }
	parts[part] = ofs;
}
end_oseq() {
	divert_part = 0;
	ofs = saveofs;
}

putpart(n) {
	FILE *fs;
	register int c;

	fs = parts[n];
	rewind(fs);
	while ((c=getc(fs))!=EOF)
		putc(c,ofs);
	fclose(fs);
	sprintf(pname,"part%d",n);
	unlink(pname);
}
